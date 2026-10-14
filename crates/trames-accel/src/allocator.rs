// SPDX-License-Identifier: GPL-3.0-or-later
//! Versioned original-mallocng hot paths over the authoritative guest heap.
use crate::native::{Error, Metrics, Return};
use std::sync::atomic::Ordering;
use trames_memory::{MemoryError, NativeAccessError, NativeMemory};
use trames_native::allocator::{Access, AccessFailure};

pub const AUXV_KEY: u64 = 0x5452_0007;
pub const PROFILE: u64 = 0x0001_0005;
/// Original source/layout identity, shared with the checked guest adapter.
pub const COOKIE: u64 = 0x1250_0040_03a0_0012;
const MMAP_THRESHOLD: u64 = 131052;

#[derive(Clone, Copy, Default)]
struct Write {
    address: u64,
    width: usize,
    value: u64,
}
enum Failure {
    Access(NativeAccessError),
    Fallback,
}
struct Transport<'a, M> {
    memory: &'a mut M,
    writes: [Write; 16],
    count: usize,
    committed: bool,
    failure: Option<Failure>,
    frame: Option<u64>,
}
impl<M: NativeMemory> Transport<'_, M> {
    fn failed<T>(&mut self, result: Result<T, NativeAccessError>) -> Result<T, AccessFailure> {
        result.map_err(|error| {
            self.failure = Some(Failure::Access(error));
            AccessFailure
        })
    }
    fn admit(&mut self, address: u64, width: usize) -> Result<(), AccessFailure> {
        let result = self.memory.native_output_range(address, width);
        if !self.failed(result)? {
            self.failure = Some(Failure::Fallback);
            return Err(AccessFailure);
        }
        Ok(())
    }
    fn flush(&mut self) -> Result<(), AccessFailure> {
        // Check every output before the first guest mutation. Mapping changes
        // require coordinated checkpoints; admitted resident writes cannot I/O.
        for i in 0..self.count {
            let w = self.writes[i];
            self.admit(w.address, w.width)?;
        }
        self.committed = true;
        for i in 0..self.count {
            let w = self.writes[i];
            let result = self
                .memory
                .write(w.address, &w.value.to_le_bytes()[..w.width]);
            self.failed(result.map_err(Into::into))?;
        }
        self.count = 0;
        Ok(())
    }
}
impl<M: NativeMemory> Access for Transport<'_, M> {
    fn load(&mut self, address: u64, width: usize) -> Result<u64, AccessFailure> {
        if !matches!(width, 1 | 2 | 4 | 8) {
            self.failure = Some(Failure::Fallback);
            return Err(AccessFailure);
        }
        let mut bytes = [0; 8];
        let result = if self.committed {
            self.memory
                .read(address, &mut bytes[..width])
                .map_err(Into::into)
        } else {
            self.memory.native_read(address, &mut bytes[..width])
        };
        self.failed(result)?;
        for w in &self.writes[..self.count] {
            for (i, b) in bytes[..width].iter_mut().enumerate() {
                let a = address.wrapping_add(i as u64);
                if a >= w.address && a - w.address < (w.width as u64) {
                    *b = w.value.to_le_bytes()[(a - w.address) as usize];
                }
            }
        }
        Ok(u64::from_le_bytes(bytes))
    }
    fn store(&mut self, address: u64, width: usize, value: u64) -> Result<(), AccessFailure> {
        if !matches!(width, 1 | 2 | 4 | 8) || self.count == self.writes.len() {
            self.failure = Some(Failure::Fallback);
            return Err(AccessFailure);
        }
        self.writes[self.count] = Write {
            address,
            width,
            value,
        };
        self.count += 1;
        Ok(())
    }
    fn cas(&mut self, address: u64, old: u32, new: u32) -> Result<u32, AccessFailure> {
        if !self.committed {
            self.admit(address, 4)?;
            if let Some(frame) = self.frame {
                self.admit(frame + 16, 16)?;
            }
            self.flush()?;
        }
        self.memory.fence(Ordering::SeqCst);
        let result = self.memory.atomic_update(address, 4, |bytes| {
            let value = u32::from_le_bytes(bytes.try_into().expect("four-byte CAS"));
            if value == old {
                bytes.copy_from_slice(&new.to_le_bytes());
            }
            (value, value == old)
        });
        self.memory.fence(Ordering::SeqCst);
        self.failed(result.map_err(Into::into))
    }
}
pub(crate) fn step(
    memory: &mut impl NativeMemory,
    function: u64,
    args: [u64; 3],
    metrics: &mut Metrics,
) -> Result<Return, Error> {
    let [mut value, mut context, third] = args;
    let fallback = || Return {
        value: 0,
        fallback: true,
    };
    if function == 1 && (third != COOKIE || value >= MMAP_THRESHOLD) {
        return Ok(fallback());
    }
    if context & 7 != 0 {
        return Ok(fallback());
    }
    let mut index = 0;
    let mut counter = 0;
    let (mt, frame) = if function == 3 {
        if context != 0 || third != 0 {
            return Ok(fallback());
        }
        let state = memory.native_read_array::<40>(value)?;
        let word = |i: usize| u64::from_le_bytes(state[i * 8..i * 8 + 8].try_into().unwrap());
        if word(0) != COOKIE || word(1) >= MMAP_THRESHOLD || word(3) > 31 {
            return Ok(fallback());
        }
        value = word(1);
        context = word(2);
        index = word(3);
        counter = word(4);
        (true, None)
    } else if function == 2 {
        if value == 0 {
            return Ok(Return {
                value: 0,
                fallback: false,
            });
        }
        let header = memory.native_read_array::<16>(third)?;
        if u64::from_le_bytes(header[..8].try_into().unwrap()) != COOKIE {
            return Ok(fallback());
        }
        (
            u64::from_le_bytes(header[8..].try_into().unwrap()) != 0,
            Some(third),
        )
    } else {
        (true, None)
    };
    let mut transport = Transport {
        memory,
        writes: [Write::default(); 16],
        count: 0,
        committed: false,
        failure: None,
        frame,
    };
    let result = trames_native::allocator::execute(
        &mut transport,
        function as u32,
        value,
        context,
        mt,
        index,
        counter,
    );
    if let Some(failure) = transport.failure.take() {
        return match failure {
            Failure::Fallback if !transport.committed => Ok(fallback()),
            Failure::Access(error) if !transport.committed => Err(error.into()),
            _ => Err(Error::Memory(MemoryError::InvalidRange)),
        };
    }
    if result.status != 0 {
        return if transport.committed {
            Err(Error::Memory(MemoryError::InvalidRange))
        } else {
            Ok(fallback())
        };
    }
    if function == 2 && result.value == 2 {
        if !transport.committed {
            return Ok(fallback());
        }
        let frame = frame.expect("free continuation frame");
        transport
            .memory
            .write_array(frame + 16, result.meta.to_le_bytes())?;
        transport
            .memory
            .write_array(frame + 24, result.index.to_le_bytes())?;
        metrics.allocator_free_resumes = metrics.allocator_free_resumes.saturating_add(1);
        return Ok(Return {
            value: 2,
            fallback: false,
        });
    }
    if function == 1 && (result.meta >= (1u64 << 48) || result.index > 31) {
        return Ok(fallback());
    }
    if transport.flush().is_err() {
        return match transport.failure.take() {
            Some(Failure::Fallback) if !transport.committed => Ok(fallback()),
            Some(Failure::Access(error)) if !transport.committed => Err(error.into()),
            _ => Err(Error::Memory(MemoryError::InvalidRange)),
        };
    }
    if function == 3 {
        metrics.allocator_allocations = metrics.allocator_allocations.saturating_add(1);
    } else if function == 1 {
        metrics.allocator_slot_reservations = metrics.allocator_slot_reservations.saturating_add(1);
    } else {
        metrics.allocator_frees = metrics.allocator_frees.saturating_add(1);
    }
    Ok(Return {
        value: if function == 1 {
            result.meta | (result.index << 48) | ((result.counter & 255) << 53)
        } else {
            result.value
        },
        fallback: false,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use trames_memory::{Access as MemoryAccess, CpuMemory, Transfer};
    use trames_memory::{Memory, Permissions, PAGE_SIZE};
    struct Conflict {
        memory: Memory,
        raced: bool,
    }
    impl CpuMemory for Conflict {
        fn read(&self, a: u64, b: &mut [u8]) -> Result<(), MemoryError> {
            self.memory.read(a, b)
        }
        fn write(&mut self, a: u64, b: &[u8]) -> Result<(), MemoryError> {
            self.memory.write(a, b)
        }
        fn check(&self, a: u64, n: usize, x: MemoryAccess) -> Result<(), MemoryError> {
            self.memory.check(a, n, x)
        }
        fn check_translation(&self, a: u64) -> Result<(), MemoryError> {
            self.memory.check_translation(a)
        }
        fn fetch_u32(&self, a: u64) -> Result<u32, MemoryError> {
            self.memory.fetch_u32(a)
        }
        fn instruction_identity(&self) -> (u64, u64, u64) {
            self.memory.instruction_identity()
        }
        fn instruction_page_identity(&self, a: u64) -> Result<(u64, u64, u64), MemoryError> {
            self.memory.instruction_page_identity(a)
        }
        fn atomic_update<T>(
            &mut self,
            a: u64,
            n: usize,
            f: impl FnOnce(&mut [u8]) -> (T, bool),
        ) -> Result<T, MemoryError> {
            if !self.raced {
                self.raced = true;
                // Peer allocations consume the remaining available slot,
                // activate freed slots and consume those too, after this free
                // observed the old freed mask. The caller's slot stays live.
                self.memory.write_array(a - 4, 0u32.to_le_bytes())?;
                self.memory.write_array(a, 0u32.to_le_bytes())?;
            }
            self.memory.atomic_update(a, n, f)
        }
        fn exclusive_read(&self, a: u64, b: &mut [u8]) -> Result<u128, MemoryError> {
            self.memory.exclusive_read(a, b)
        }
        fn exclusive_write(
            &mut self,
            a: u64,
            b: &[u8],
            t: Option<u128>,
        ) -> Result<bool, MemoryError> {
            self.memory.exclusive_write(a, b, t)
        }
    }
    impl NativeMemory for Conflict {
        fn native_writable_range(&self, a: u64, n: usize) -> Result<bool, NativeAccessError> {
            NativeMemory::native_writable_range(&self.memory, a, n)
        }
        fn native_output_range(&self, a: u64, n: usize) -> Result<bool, NativeAccessError> {
            NativeMemory::native_output_range(&self.memory, a, n)
        }
        fn native_read(&self, a: u64, b: &mut [u8]) -> Result<(), NativeAccessError> {
            self.memory.native_read(a, b)
        }
        fn with_read_chunk<T>(
            &self,
            a: u64,
            n: usize,
            f: impl FnOnce(&[u8]) -> T,
        ) -> Result<(T, bool), NativeAccessError> {
            NativeMemory::with_read_chunk(&self.memory, a, n, f)
        }
        fn with_read_pair<T>(
            &self,
            a: u64,
            b: u64,
            n: usize,
            f: impl FnOnce(&[u8], &[u8]) -> T,
        ) -> Result<(T, bool, bool), NativeAccessError> {
            self.memory.with_read_pair(a, b, n, f)
        }
        fn fill_with(
            &mut self,
            a: u64,
            n: usize,
            f: impl FnMut(&mut [u8]),
        ) -> Result<Transfer, NativeAccessError> {
            NativeMemory::fill_with(&mut self.memory, a, n, f)
        }
        fn copy_with(
            &mut self,
            a: u64,
            b: u64,
            n: usize,
            f: impl FnMut(&mut [u8], &[u8]),
        ) -> Result<Transfer, NativeAccessError> {
            NativeMemory::copy_with(&mut self.memory, a, b, n, f)
        }
    }
    fn heap() -> Memory {
        let mut memory = Memory::default();
        memory.map(0x1000, 4 * PAGE_SIZE, Permissions::RW).unwrap();
        for (address, value) in [
            (0x1000, 0x55aa),
            (0x1000 + 80 + 8 * 10, 0x3018),
            (0x3018 + 16, 0x4000),
            (0x3018 + 32, 3 | (1 << 5) | (10 << 6) | (1 << 12)),
            (0x4000, 0x3018),
            (0x3000, 0x55aa),
            (0x2000, COOKIE),
            (0x2008, 1),
        ] {
            memory.write_array(address, value.to_le_bytes()).unwrap();
        }
        memory.write_array(0x100c, 1u32.to_le_bytes()).unwrap();
        memory.write(0x4008, &[3]).unwrap();
        memory.write_array(0x3030, 12u32.to_le_bytes()).unwrap();
        memory.write_array(0x3034, 1u32.to_le_bytes()).unwrap();
        memory
    }
    fn allocate(memory: &mut Memory, metrics: &mut Metrics) -> Return {
        memory.write_array(0x2200, COOKIE.to_le_bytes()).unwrap();
        memory.write_array(0x2208, 157u64.to_le_bytes()).unwrap();
        let reserve = step(memory, 1, [157, 0x1000, COOKIE], metrics).unwrap();
        assert!(!reserve.fallback);
        memory
            .write_array(0x2210, (reserve.value & 0x0000ffffffffffff).to_le_bytes())
            .unwrap();
        memory
            .write_array(0x2218, ((reserve.value >> 48) & 31).to_le_bytes())
            .unwrap();
        memory
            .write_array(0x2220, ((reserve.value >> 53) & 255).to_le_bytes())
            .unwrap();
        step(memory, 3, [0x2200, 0, 0], metrics).unwrap()
    }
    #[test]
    fn original_enframe_and_atomic_free_share_guest_headers_and_masks() {
        let mut memory = heap();
        let mut metrics = Metrics::default();
        let result = allocate(&mut memory, &mut metrics);
        assert_eq!(
            result,
            Return {
                value: 0x41a0,
                fallback: false
            }
        );
        assert_eq!(u32::from_le_bytes(memory.read_array(0x3030).unwrap()), 8);
        assert_eq!(memory.read_array::<4>(0x419c).unwrap(), [0, 162, 25, 0]);
        assert_eq!(
            step(&mut memory, 2, [result.value, 0x1000, 0x2000], &mut metrics).unwrap(),
            Return {
                value: 0,
                fallback: false
            }
        );
        assert_eq!(memory.read_array::<3>(0x419d).unwrap(), [255, 0, 0]);
        assert_eq!(u32::from_le_bytes(memory.read_array(0x3034).unwrap()), 5);
        assert_eq!(
            (metrics.allocator_allocations, metrics.allocator_frees),
            (1, 1)
        );
    }
    #[test]
    fn failed_cas_resumes_original_slow_free_with_saved_metadata_and_index() {
        let mut memory = heap();
        let mut metrics = Metrics::default();
        let p = allocate(&mut memory, &mut metrics).value;
        let mut memory = Conflict {
            memory,
            raced: false,
        };
        assert_eq!(
            step(&mut memory, 2, [p, 0x1000, 0x2000], &mut metrics).unwrap(),
            Return {
                value: 2,
                fallback: false
            }
        );
        assert_eq!(
            u64::from_le_bytes(memory.memory.read_array(0x2010).unwrap()),
            0x3018
        );
        assert_eq!(
            u64::from_le_bytes(memory.memory.read_array(0x2018).unwrap()),
            2
        );
        assert_eq!(memory.memory.read_array::<3>(p - 3).unwrap(), [255, 0, 0]);
        assert_eq!(metrics.allocator_free_resumes, 1);
    }
    #[test]
    fn slow_free_and_invalid_metadata_fall_back_without_partial_invalidation() {
        let mut memory = heap();
        let mut metrics = Metrics::default();
        let pointer = allocate(&mut memory, &mut metrics).value;
        let header = memory.read_array::<4>(pointer - 4).unwrap();
        memory.write_array(0x3034, 0u32.to_le_bytes()).unwrap();
        assert!(
            step(&mut memory, 2, [pointer, 0x1000, 0x2000], &mut metrics)
                .unwrap()
                .fallback
        );
        assert_eq!(memory.read_array::<4>(pointer - 4).unwrap(), header);
        memory.write_array(0x3000, 0u64.to_le_bytes()).unwrap();
        assert!(
            step(&mut memory, 2, [pointer, 0x1000, 0x2000], &mut metrics)
                .unwrap()
                .fallback
        );
        assert_eq!(memory.read_array::<4>(pointer - 4).unwrap(), header);
        assert!(
            step(
                &mut memory,
                1,
                [MMAP_THRESHOLD, 0x1000, COOKIE],
                &mut metrics
            )
            .unwrap()
            .fallback
        );
        assert!(
            step(&mut memory, 1, [7, 0x1000, COOKIE + 1], &mut metrics)
                .unwrap()
                .fallback
        );
    }
    #[test]
    fn output_preflight_preserves_available_mask_and_parallel_frees_are_coherent() {
        let mut memory = heap();
        let mut metrics = Metrics::default();
        memory.write_array(0x2200, COOKIE.to_le_bytes()).unwrap();
        memory.write_array(0x2208, 157u64.to_le_bytes()).unwrap();
        memory.protect(0x3000, PAGE_SIZE, Permissions::R).unwrap();
        assert!(step(&mut memory, 1, [157, 0x1000, COOKIE], &mut metrics).is_err());
        assert_eq!(u32::from_le_bytes(memory.read_array(0x3030).unwrap()), 12);
        memory.protect(0x3000, PAGE_SIZE, Permissions::RW).unwrap();
        let p = allocate(&mut memory, &mut metrics).value;
        let q = allocate(&mut memory, &mut metrics).value;
        memory.write_array(0x2100, COOKIE.to_le_bytes()).unwrap();
        memory.write_array(0x2108, 1u64.to_le_bytes()).unwrap();
        let mut registry = trames_memory::parallel::Registry::default();
        let first = memory.parallel_view(&mut registry).unwrap();
        let second = first.clone();
        let barrier = std::sync::Arc::new(std::sync::Barrier::new(2));
        let handles = [(first, p, 0x2000), (second, q, 0x2100)]
            .into_iter()
            .map(|(mut view, pointer, frame)| {
                let barrier = barrier.clone();
                std::thread::spawn(move || {
                    barrier.wait();
                    let mut m = Metrics::default();
                    assert_eq!(
                        step(&mut view, 2, [pointer, 0x1000, frame], &mut m).unwrap(),
                        Return {
                            value: 0,
                            fallback: false
                        }
                    );
                })
            })
            .collect::<Vec<_>>();
        for handle in handles {
            handle.join().unwrap();
        }
        memory.commit_parallel().unwrap();
        assert_eq!(u32::from_le_bytes(memory.read_array(0x3034).unwrap()), 13);
        for pointer in [p, q] {
            assert_eq!(memory.read_array::<3>(pointer - 3).unwrap(), [255, 0, 0]);
        }
    }
}
