// SPDX-License-Identifier: GPL-3.0-or-later
//! NativeCall v1, pure-memory profile. The signed-in catalog authorizes only
//! bounded pure operations; matching guest code never grants host privileges.
use serde::Serialize;
use std::time::Instant;
use trames_memory::{Access, Memory, MemoryError, PAGE_SIZE};
pub const AUXV_KEY: u64 = 0x5452_0001;
pub const PROFILE: u64 = 0x0001_0001; // protocol 1, pure module 1
pub const INSTRUCTION: u32 = 0xd44a8a40; // HLT #0x5452, opt-in virtual encoding
pub const MAX_BYTES: usize = 64 * 1024;
#[derive(Default, Debug, Serialize)]
pub struct Metrics {
    pub calls: u64,
    pub fallbacks: u64,
    pub rejected: u64,
    pub faults: u64,
    pub bytes_read: u64,
    pub bytes_written: u64,
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
        if module != PROFILE || !(1..=5).contains(&function) || pc < 12 {
            return false;
        }
        let words = [
            0xd2800030,
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
        let result = self.execute(memory, function, a);
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
    fn execute(
        &mut self,
        memory: &mut Memory,
        function: u64,
        a: [u64; 3],
    ) -> Result<Return, Error> {
        let (dst, src, len) = (a[0], a[1], a[2]);
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
        if function != 5 && len > MAX_BYTES as u64 {
            return fallback();
        }
        let len = len as usize;
        match function {
            1 | 2 => {
                // memcpy / memmove: bounded snapshot handles overlap and aliases.
                if len == 0 || (function == 2 && dst == src) {
                    return done(dst);
                }
                memory.check(src, len, Access::Read)?;
                memory.check(dst, len, Access::Write)?;
                let mut input = vec![0; len];
                memory.read(src, &mut input)?;
                self.metrics.bytes_read = self.metrics.bytes_read.saturating_add(len as u64);
                let mut output = vec![0; len];
                trames_native::copy(&mut output, &input);
                memory.write(dst, &output)?;
                self.metrics.bytes_written = self.metrics.bytes_written.saturating_add(len as u64);
                done(dst)
            }
            3 => {
                // memset: low unsigned byte only, original return pointer including tag.
                if len == 0 {
                    return done(dst);
                }
                memory.check(dst, len, Access::Write)?;
                let mut bytes = vec![0; len];
                trames_native::fill(&mut bytes, src as u8);
                memory.write(dst, &bytes)?;
                self.metrics.bytes_written = self.metrics.bytes_written.saturating_add(len as u64);
                done(dst)
            }
            4 => {
                // memcmp stops at mismatch without validating unreachable later pages.
                let mut offset = 0;
                let mut left = [0; PAGE_SIZE as usize];
                let mut right = [0; PAGE_SIZE as usize];
                while offset < len {
                    let la = dst
                        .checked_add(offset as u64)
                        .ok_or(MemoryError::AddressRange(dst))?;
                    let ra = src
                        .checked_add(offset as u64)
                        .ok_or(MemoryError::AddressRange(src))?;
                    let count = (len - offset)
                        .min((PAGE_SIZE - la % PAGE_SIZE) as usize)
                        .min((PAGE_SIZE - ra % PAGE_SIZE) as usize);
                    memory.read(la, &mut left[..count])?;
                    memory.read(ra, &mut right[..count])?;
                    self.metrics.bytes_read =
                        self.metrics.bytes_read.saturating_add(2 * count as u64);
                    let value = trames_native::compare(&left[..count], &right[..count]);
                    if value != 0 {
                        return done(value as u32 as u64);
                    } // C int in w0
                    offset += count;
                }
                done(0)
            }
            5 => {
                // strlen: page-bounded scan; over-limit falls back without effects.
                let mut page = [0; PAGE_SIZE as usize];
                let mut offset = 0;
                while offset < MAX_BYTES {
                    let address = dst
                        .checked_add(offset as u64)
                        .ok_or(MemoryError::AddressRange(dst))?;
                    let count =
                        (MAX_BYTES - offset).min((PAGE_SIZE - address % PAGE_SIZE) as usize);
                    memory.read(address, &mut page[..count])?;
                    self.metrics.bytes_read = self.metrics.bytes_read.saturating_add(count as u64);
                    if let Some(index) = trames_native::first_zero(&page[..count]) {
                        return done((offset + index) as u64);
                    }
                    offset += count;
                }
                fallback()
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
        for (module, function) in [(PROFILE + 1, 1), (PROFILE, 6), (PROFILE, 0)] {
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
    }
}
