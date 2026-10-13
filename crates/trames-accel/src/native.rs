// SPDX-License-Identifier: GPL-3.0-or-later
//! NativeCall v1, pure-memory profile. The signed-in catalog authorizes only
//! bounded pure operations; matching guest code never grants host privileges.
use serde::Serialize;
use std::time::Instant;
use trames_memory::{Memory, MemoryError, Transfer, PAGE_SIZE};
pub const AUXV_KEY: u64 = 0x5452_0001;
/// Optional function bitmap: bit (id - 1). Absent on old hosts.
pub const FEATURES_AUXV_KEY: u64 = 0x5452_0002;
pub const FEATURES: u64 = (1 << 10) - 1;
pub const PROFILE: u64 = 0x0001_0001; // protocol 1, pure module 1
pub const INSTRUCTION: u32 = 0xd44a8a40; // HLT #0x5452, opt-in virtual encoding
pub const MAX_BYTES: usize = 64 * 1024;
#[derive(Default, Debug, Serialize)]
pub struct Metrics {
    pub calls: u64,
    pub function_calls: [u64; 10],
    pub callback_calls: u64,
    pub callback_requests: u64,
    pub callback_yields: u64,
    pub callback_sorts: u64,
    pub callback_searches: u64,
    pub callback_state_bytes: u64,
    pub numeric_calls: u64,
    pub numeric_conversions: u64,
    pub numeric_replays: u64,
    pub numeric_input_bytes: u64,
    pub numeric_result_bytes: u64,
    pub fallbacks: u64,
    pub rejected: u64,
    pub faults: u64,
    pub bytes_read: u64,
    pub bytes_written: u64,
    pub direct_bytes_read: u64,
    pub direct_bytes_written: u64,
    pub buffered_bytes_read: u64,
    pub buffered_bytes_written: u64,
    pub nanoseconds: u64,
}
#[derive(Debug)]
pub enum Error {
    InvalidEntry,
    Memory(MemoryError),
}
impl From<MemoryError> for Error {
    fn from(value: MemoryError) -> Self {
        Self::Memory(value)
    }
}
#[derive(Debug, PartialEq, Eq)]
pub struct Return {
    pub value: u64,
    pub fallback: bool,
}
#[derive(Default, Debug)]
pub struct Registry {
    pub metrics: Metrics,
}
impl Registry {
    /// Verify the immutable v1 stub contract in currently executable memory.
    /// This is rechecked on every entry, including fork, remap and self-modification.
    fn valid(memory: &Memory, pc: u64, module: u64, function: u64) -> bool {
        let allowed = (module == PROFILE && (1..=10).contains(&function))
            || (module == crate::callbacks::PROFILE && function == 1)
            || (module == crate::numeric::PROFILE && function == 1);
        if !allowed || pc < 12 {
            return false;
        }
        let words = [
            0xd2800010 | ((module as u32 & 0xffff) << 5),
            0xf2a00030,
            0xd2800011 | (function as u32) << 5,
            INSTRUCTION,
            0xd65f03c0,
        ];
        words
            .into_iter()
            .enumerate()
            .all(|(i, word)| memory.fetch_u32(pc - 12 + i as u64 * 4) == Ok(word))
    }
    pub fn invoke(
        &mut self,
        memory: &mut Memory,
        pc: u64,
        module: u64,
        function: u64,
        a: [u64; 3],
    ) -> Result<Return, Error> {
        if !Self::valid(memory, pc, module, function) {
            self.metrics.rejected = self.metrics.rejected.saturating_add(1);
            return Err(Error::InvalidEntry);
        }
        self.metrics.calls = self.metrics.calls.saturating_add(1);
        let began = Instant::now();
        let result = if module == crate::callbacks::PROFILE {
            self.metrics.callback_calls = self.metrics.callback_calls.saturating_add(1);
            crate::callbacks::step(memory, a, &mut self.metrics)
        } else if module == crate::numeric::PROFILE {
            self.metrics.numeric_calls = self.metrics.numeric_calls.saturating_add(1);
            crate::numeric::parse(memory, a, &mut self.metrics)
        } else {
            let count = &mut self.metrics.function_calls[function as usize - 1];
            *count = count.saturating_add(1);
            self.execute(memory, function, a)
        };
        self.metrics.nanoseconds = self
            .metrics
            .nanoseconds
            .saturating_add(began.elapsed().as_nanos().min(u64::MAX as u128) as u64);
        match &result {
            Ok(Return { fallback: true, .. }) => {
                self.metrics.fallbacks = self.metrics.fallbacks.saturating_add(1)
            }
            Err(_) => self.metrics.faults = self.metrics.faults.saturating_add(1),
            _ => {}
        }
        result
    }
    fn record_transfer(&mut self, transfer: Transfer) {
        self.metrics.bytes_read = self
            .metrics
            .bytes_read
            .saturating_add(transfer.direct_read + transfer.buffered_read);
        self.metrics.bytes_written = self
            .metrics
            .bytes_written
            .saturating_add(transfer.direct_write + transfer.buffered_write);
        self.metrics.direct_bytes_read = self
            .metrics
            .direct_bytes_read
            .saturating_add(transfer.direct_read);
        self.metrics.direct_bytes_written = self
            .metrics
            .direct_bytes_written
            .saturating_add(transfer.direct_write);
        self.metrics.buffered_bytes_read = self
            .metrics
            .buffered_bytes_read
            .saturating_add(transfer.buffered_read);
        self.metrics.buffered_bytes_written = self
            .metrics
            .buffered_bytes_written
            .saturating_add(transfer.buffered_write);
    }
    fn record_read(&mut self, count: usize, direct: bool) {
        self.record_transfer(Transfer {
            direct_read: if direct { count as u64 } else { 0 },
            buffered_read: if direct { 0 } else { count as u64 },
            ..Transfer::default()
        });
    }
    fn execute(
        &mut self,
        memory: &mut Memory,
        function: u64,
        a: [u64; 3],
    ) -> Result<Return, Error> {
        let (dst, src, length) = (a[0], a[1], a[2]);
        let fallback = || {
            Ok(Return {
                value: dst,
                fallback: true,
            })
        };
        let done = |value| {
            Ok(Return {
                value,
                fallback: false,
            })
        };
        if matches!(function, 1..=4 | 6 | 7 | 10) && length > MAX_BYTES as u64 {
            return fallback();
        }
        let len = length as usize;
        match function {
            1 | 2 => {
                if len == 0 || (function == 2 && dst == src) {
                    return done(dst);
                }
                let transfer = memory.copy_with(dst, src, len, trames_native::copy)?;
                self.record_transfer(transfer);
                done(dst)
            }
            3 => {
                let transfer =
                    memory.fill_with(dst, len, |bytes| trames_native::fill(bytes, src as u8))?;
                self.record_transfer(transfer);
                done(dst)
            }
            4 | 9 | 10 => {
                // Compare only reached page fragments; mismatch/NUL ends the scan.
                let limit = if function == 9 { MAX_BYTES } else { len };
                let mut offset = 0;
                while offset < limit {
                    let la = (dst & 0x00ffffffffffffff) + offset as u64;
                    let ra = (src & 0x00ffffffffffffff) + offset as u64;
                    let count = (limit - offset)
                        .min((PAGE_SIZE - la % PAGE_SIZE) as usize)
                        .min((PAGE_SIZE - ra % PAGE_SIZE) as usize);
                    let (right, ld) = memory.with_read_chunk(la, count, |left| {
                        memory.with_read_chunk(ra, count, |right| {
                            if function == 4 {
                                let value = trames_native::compare(left, right);
                                (value != 0).then_some(value)
                            } else {
                                trames_native::compare_strings(left, right)
                            }
                        })
                    })?;
                    let (value, rd) = right?;
                    self.record_read(count, ld);
                    self.record_read(count, rd);
                    if let Some(value) = value {
                        return done(value as u32 as u64);
                    }
                    offset += count;
                }
                if function == 9 {
                    fallback()
                } else {
                    done(0)
                }
            }
            5 | 8 => {
                // strnlen may stop before a huge bound. Only a scan exhausted at
                // the native limit requests the untouched original implementation.
                let limit = if function == 8 {
                    src.min(MAX_BYTES as u64) as usize
                } else {
                    MAX_BYTES
                };
                let mut offset = 0;
                while offset < limit {
                    let address = (dst & 0x00ffffffffffffff) + offset as u64;
                    let count = (limit - offset).min((PAGE_SIZE - address % PAGE_SIZE) as usize);
                    let (index, direct) =
                        memory.with_read_chunk(address, count, trames_native::first_zero)?;
                    self.record_read(count, direct);
                    if let Some(index) = index {
                        return done((offset + index) as u64);
                    }
                    offset += count;
                }
                if function == 8 && src <= MAX_BYTES as u64 {
                    done(src)
                } else {
                    fallback()
                }
            }
            6 | 7 => {
                let mut remaining = len;
                while remaining != 0 {
                    let (offset, count) = if function == 7 {
                        let end = (dst & 0x00ffffffffffffff) + remaining as u64;
                        let count = remaining.min(((end - 1) % PAGE_SIZE + 1) as usize);
                        (remaining - count, count)
                    } else {
                        let offset = len - remaining;
                        let at = (dst & 0x00ffffffffffffff) + offset as u64;
                        (offset, remaining.min((PAGE_SIZE - at % PAGE_SIZE) as usize))
                    };
                    let (index, direct) = memory.with_read_chunk(
                        (dst & 0x00ffffffffffffff) + offset as u64,
                        count,
                        |bytes| {
                            if function == 6 {
                                trames_native::first_byte(bytes, src as u8)
                            } else {
                                trames_native::last_byte(bytes, src as u8)
                            }
                        },
                    )?;
                    self.record_read(count, direct);
                    if let Some(index) = index {
                        return done(dst.wrapping_add((offset + index) as u64));
                    }
                    remaining -= count;
                }
                done(0)
            }
            _ => unreachable!("signed-in pure profile"),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use trames_memory::Permissions;
    fn memory(function: u64) -> Memory {
        let mut m = Memory::default();
        m.map(0x1000, PAGE_SIZE, Permissions::RW).unwrap();
        m.map(0x2000, PAGE_SIZE * 2, Permissions::RW).unwrap();
        let words = [
            0xd2800030u32,
            0xf2a00030,
            0xd2800011 | (function as u32) << 5,
            INSTRUCTION,
            0xd65f03c0,
        ];
        for (i, word) in words.into_iter().enumerate() {
            m.write(0x1000 + i as u64 * 4, &word.to_le_bytes()).unwrap();
        }
        m.protect(0x1000, PAGE_SIZE, Permissions::RX).unwrap();
        m
    }
    #[test]
    fn rejection_and_limit_fallback_have_no_memory_effects() {
        let mut m = memory(1);
        let mut r = Registry::default();
        m.write(0x2000, b"before").unwrap();
        for (module, function) in [(PROFILE + 1, 1), (PROFILE, 11), (PROFILE, 0)] {
            assert!(matches!(
                r.invoke(&mut m, 0x100c, module, function, [0x2000, 1, 6]),
                Err(Error::InvalidEntry)
            ));
        }
        assert!(
            r.invoke(&mut m, 0x100c, PROFILE, 1, [1, 2, MAX_BYTES as u64 + 1])
                .unwrap()
                .fallback
        );
        assert_eq!(m.read_array::<6>(0x2000).unwrap(), *b"before");
        // Caller bytes are revalidated after executable mutation, not just once.
        m.protect(0x1000, PAGE_SIZE, Permissions::RW).unwrap();
        m.write(0x1000, &0xd503201fu32.to_le_bytes()).unwrap();
        m.protect(0x1000, PAGE_SIZE, Permissions::RX).unwrap();
        assert!(matches!(
            r.invoke(&mut m, 0x100c, PROFILE, 1, [0x2000, 0x3000, 6]),
            Err(Error::InvalidEntry)
        ));
        assert_eq!(r.metrics.fallbacks, 1);
        assert_eq!(r.metrics.rejected, 4);
    }
    #[test]
    fn early_stops_and_precise_fault_leave_destination_untouched() {
        let mut m = memory(5);
        let mut r = Registry::default();
        m.write(0x3fff, &[0]).unwrap();
        assert_eq!(
            r.invoke(&mut m, 0x100c, PROFILE, 5, [0x3fff, 0, 0])
                .unwrap()
                .value,
            0
        );
        m.write(0x3fff, &[1]).unwrap();
        assert!(matches!(
            r.invoke(&mut m, 0x100c, PROFILE, 5, [0x3fff, 0, 0]),
            Err(Error::Memory(MemoryError::Unmapped(0x4000)))
        ));
        let mut m = memory(4);
        m.write(0x3fff, &[1]).unwrap();
        m.write(0x2fff, &[255]).unwrap();
        assert_eq!(
            r.invoke(&mut m, 0x100c, PROFILE, 4, [0x3fff, 0x2fff, 512])
                .unwrap()
                .value as u32 as i32,
            -254
        );
        let mut m = memory(1);
        m.write(0x2000, b"before").unwrap();
        assert!(matches!(
            r.invoke(&mut m, 0x100c, PROFILE, 1, [0x2000, 0x3fff, 6]),
            Err(Error::Memory(MemoryError::Unmapped(0x4000)))
        ));
        assert_eq!(m.read_array::<6>(0x2000).unwrap(), *b"before");
    }
    #[test]
    fn copy_overlap_cow_and_tagged_return_are_preserved() {
        let mut m = memory(2);
        let mut r = Registry::default();
        m.write(0x2ffc, b"abcdefgh").unwrap();
        let parent = m.clone();
        let tagged = 0xab00_0000_0000_2ffe;
        assert_eq!(
            r.invoke(&mut m, 0x100c, PROFILE, 2, [tagged, 0x2ffc, 6])
                .unwrap()
                .value,
            tagged
        );
        assert_eq!(m.read_array::<8>(0x2ffc).unwrap(), *b"ababcdef");
        assert_eq!(parent.read_array::<8>(0x2ffc).unwrap(), *b"abcdefgh");
        assert_eq!(r.metrics.buffered_bytes_read, 0);
        assert_eq!(r.metrics.buffered_bytes_written, 0);
        assert_eq!(r.metrics.direct_bytes_written, 6);
    }
    #[test]
    fn searches_stop_at_reached_pages_preserve_tags_and_large_bounds_fall_back() {
        let mut r = Registry::default();
        for function in [6, 7] {
            let mut m = memory(function);
            m.write(0x3fff, &[0xff]).unwrap();
            let tagged = 0xab00_0000_0000_3fff;
            assert_eq!(
                r.invoke(&mut m, 0x100c, PROFILE, function, [tagged, 0x1ff, 1])
                    .unwrap()
                    .value,
                tagged
            );
            assert_eq!(
                r.invoke(&mut m, 0x100c, PROFILE, function, [1, 1, 0])
                    .unwrap()
                    .value,
                0
            );
            assert!(
                r.invoke(
                    &mut m,
                    0x100c,
                    PROFILE,
                    function,
                    [1, 1, MAX_BYTES as u64 + 1]
                )
                .unwrap()
                .fallback
            );
        }
        let mut m = memory(6);
        m.write(0x3fff, &[17]).unwrap();
        assert_eq!(
            r.invoke(&mut m, 0x100c, PROFILE, 6, [0x3fff, 17, 1024])
                .unwrap()
                .value,
            0x3fff
        );
        assert!(matches!(
            r.invoke(&mut m, 0x100c, PROFILE, 6, [0x3fff, 18, 1024]),
            Err(Error::Memory(MemoryError::Unmapped(0x4000)))
        ));
        let mut m = memory(8);
        m.write(0x3ffe, &[1, 0]).unwrap();
        assert_eq!(
            r.invoke(&mut m, 0x100c, PROFILE, 8, [0x3ffe, u64::MAX, 0])
                .unwrap()
                .value,
            1
        );
        assert_eq!(
            r.invoke(&mut m, 0x100c, PROFILE, 8, [1, 0, 0])
                .unwrap()
                .value,
            0
        );
        m.write(0x3fff, &[1]).unwrap();
        assert_eq!(
            r.invoke(&mut m, 0x100c, PROFILE, 8, [0x3fff, 1, 0])
                .unwrap()
                .value,
            1
        );
        for function in [9, 10] {
            let mut m = memory(function);
            m.write(0x3fff, &[0]).unwrap();
            m.write(0x2fff, &[0]).unwrap();
            assert_eq!(
                r.invoke(&mut m, 0x100c, PROFILE, function, [0x3fff, 0x2fff, 1024])
                    .unwrap()
                    .value,
                0
            );
            m.write(0x3fff, &[255]).unwrap();
            m.write(0x2fff, &[1]).unwrap();
            assert_eq!(
                r.invoke(&mut m, 0x100c, PROFILE, function, [0x3fff, 0x2fff, 1024])
                    .unwrap()
                    .value as u32 as i32,
                254
            );
        }
        assert_eq!(r.metrics.buffered_bytes_read, 0);
    }
}
