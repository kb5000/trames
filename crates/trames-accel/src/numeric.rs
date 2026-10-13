// SPDX-License-Identifier: GPL-3.0-or-later
//! Bounded musl floating conversion over checked guest pages.
use crate::native::{Error, Metrics, Return, MAX_BYTES};
use trames_memory::{Access, Memory, PAGE_SIZE};
pub const AUXV_KEY: u64 = 0x5452_0005;
pub const PROFILE: u64 = 0x0001_0003;
const OUTPUT_BYTES: usize = 16; // tagged end pointer, Linux errno (zero: unchanged)
pub(crate) fn parse(
    memory: &mut Memory,
    a: [u64; 3],
    metrics: &mut Metrics,
) -> Result<Return, Error> {
    let [source, output, precision] = a;
    if precision > 1 {
        return Err(Error::InvalidEntry);
    }
    let fallback = || {
        Ok(Return {
            value: 0,
            fallback: true,
        })
    };
    memory.check(output, OUTPUT_BYTES, Access::Write)?;
    if !memory.native_writable_range(output, OUTPUT_BYTES)? {
        return fallback();
    }
    let base = source & 0x00ff_ffff_ffff_ffff;
    let page_limit = (PAGE_SIZE - base % PAGE_SIZE) as usize;
    let mut first = 64.min(page_limit);
    let mut parsed;
    loop {
        let (result, direct) = memory.with_read_chunk(base, first, |bytes| {
            trames_native::parse::floating(bytes, precision == 0)
        })?;
        record_read(metrics, first, direct);
        parsed = result;
        if !parsed.needs_more || first == page_limit {
            break;
        }
        first = (first * 2).min(page_limit);
        metrics.numeric_replays = metrics.numeric_replays.saturating_add(1);
    }
    let mut input = Vec::new();
    let mut supplied = first;
    if parsed.needs_more {
        let (_, direct) =
            memory.with_read_chunk(base, first, |bytes| input.extend_from_slice(bytes))?;
        record_read(metrics, first, direct);
    }
    while parsed.needs_more {
        if supplied >= MAX_BYTES {
            return fallback();
        }
        let at = base
            .checked_add(supplied as u64)
            .ok_or(Error::InvalidEntry)?;
        let count = supplied
            .max(64)
            .min(MAX_BYTES - supplied)
            .min((PAGE_SIZE - at % PAGE_SIZE) as usize);
        let (_, direct) =
            memory.with_read_chunk(at, count, |bytes| input.extend_from_slice(bytes))?;
        record_read(metrics, count, direct);
        supplied += count;
        parsed = trames_native::parse::floating(&input, precision == 0);
        metrics.numeric_replays = metrics.numeric_replays.saturating_add(1);
    }
    if parsed.consumed > supplied || !matches!(parsed.errno, 0 | 22 | 34) {
        return Err(Error::InvalidEntry);
    }
    let end = source
        .checked_add(parsed.consumed as u64)
        .ok_or(Error::InvalidEntry)?;
    let mut bytes = [0; OUTPUT_BYTES];
    bytes[..8].copy_from_slice(&end.to_le_bytes());
    bytes[8..].copy_from_slice(&(parsed.errno as u64).to_le_bytes());
    memory.write(output, &bytes)?;
    metrics.numeric_conversions = metrics.numeric_conversions.saturating_add(1);
    metrics.numeric_result_bytes = metrics
        .numeric_result_bytes
        .saturating_add(OUTPUT_BYTES as u64);
    Ok(Return {
        value: parsed.bits,
        fallback: false,
    })
}
fn record_read(metrics: &mut Metrics, count: usize, direct: bool) {
    metrics.bytes_read = metrics.bytes_read.saturating_add(count as u64);
    if direct {
        metrics.direct_bytes_read = metrics.direct_bytes_read.saturating_add(count as u64);
    } else {
        metrics.buffered_bytes_read = metrics.buffered_bytes_read.saturating_add(count as u64);
    }
    metrics.numeric_input_bytes = metrics.numeric_input_bytes.saturating_add(count as u64);
}
#[cfg(test)]
mod tests {
    use super::*;
    use trames_memory::Permissions;
    #[test]
    fn stops_at_delimiters_handles_page_split_and_keeps_tagged_end_pointer() {
        let mut m = Memory::default();
        m.map(0x1000, PAGE_SIZE * 3, Permissions::RW).unwrap();
        m.write(0x1ffd, b"1.25e+2!").unwrap();
        let mut metrics = Metrics::default();
        let r = parse(&mut m, [0xab00_0000_0000_1ffd, 0x3000, 1], &mut metrics).unwrap();
        assert_eq!(
            r,
            Return {
                value: 125f64.to_bits(),
                fallback: false
            }
        );
        assert_eq!(
            u64::from_le_bytes(m.read_array(0x3000).unwrap()),
            0xab00_0000_0000_2004
        );
        assert!(metrics.numeric_replays > 0);
        m.write(0x2ffd, b"1x").unwrap();
        m.unmap(0x3000, PAGE_SIZE).unwrap();
        let r = parse(&mut m, [0x2ffd, 0x1000, 1], &mut metrics).unwrap();
        assert_eq!(r.value, 1f64.to_bits());
    }
    #[test]
    fn ceilings_and_shared_output_fall_back_without_publishing_partial_results() {
        let mut m = Memory::default();
        m.map(0x1000, PAGE_SIZE, Permissions::RW).unwrap();
        m.map(0x4000, MAX_BYTES as u64, Permissions::RW).unwrap();
        m.write(0x1000, &[17; 16]).unwrap();
        m.write(0x4000, &vec![b'1'; MAX_BYTES]).unwrap();
        let mut metrics = Metrics::default();
        assert!(
            parse(&mut m, [0x4000, 0x1000, 1], &mut metrics)
                .unwrap()
                .fallback
        );
        assert_eq!(m.read_array::<16>(0x1000).unwrap(), [17; 16]);
        m.map_shared_anonymous(0x1000, PAGE_SIZE, Permissions::RW, true)
            .unwrap();
        m.write(0x1000, &[17; 16]).unwrap();
        assert!(
            parse(&mut m, [0x4000, 0x1000, 0], &mut metrics)
                .unwrap()
                .fallback
        );
        assert_eq!(m.read_array::<16>(0x1000).unwrap(), [17; 16]);
    }
}
