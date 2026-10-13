// SPDX-License-Identifier: GPL-3.0-or-later
//! Optional identical-source allocation/timing probe for old/new memory APIs.
//! No timing assertions; run in an isolated process after builds/tests finish.
use std::{
    alloc::{GlobalAlloc, Layout, System},
    hint::black_box,
    sync::atomic::{AtomicU64, Ordering},
    time::Instant,
};
use trames_accel::native::{Registry, INSTRUCTION, PROFILE};
use trames_memory::{Memory, Permissions, PAGE_SIZE};
struct Counting;
static ALLOCATIONS: AtomicU64 = AtomicU64::new(0);
static ALLOCATED_BYTES: AtomicU64 = AtomicU64::new(0);
// SAFETY: Forward the exact caller layouts/pointers to System. Counters do not
// allocate and use atomics; no allocator ownership or alignment is changed.
unsafe impl GlobalAlloc for Counting {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        ALLOCATIONS.fetch_add(1, Ordering::Relaxed);
        ALLOCATED_BYTES.fetch_add(layout.size() as u64, Ordering::Relaxed);
        unsafe { System.alloc(layout) }
    }
    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        unsafe { System.dealloc(ptr, layout) }
    }
    unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, size: usize) -> *mut u8 {
        ALLOCATIONS.fetch_add(1, Ordering::Relaxed);
        ALLOCATED_BYTES.fetch_add(size as u64, Ordering::Relaxed);
        unsafe { System.realloc(ptr, layout, size) }
    }
}
#[global_allocator]
static ALLOCATOR: Counting = Counting;
fn sample(name: &str, iterations: u64, mut run: impl FnMut()) {
    for _ in 0..64 {
        run();
    }
    let before = ALLOCATIONS.load(Ordering::Relaxed);
    let bytes = ALLOCATED_BYTES.load(Ordering::Relaxed);
    let start = Instant::now();
    for _ in 0..iterations {
        run();
    }
    let seconds = start.elapsed().as_secs_f64();
    let allocations = ALLOCATIONS.load(Ordering::Relaxed) - before;
    let allocated_bytes = ALLOCATED_BYTES.load(Ordering::Relaxed) - bytes;
    println!("{{\"workload\":\"{name}\",\"iterations\":{iterations},\"seconds\":{seconds},\"allocations\":{allocations},\"allocated_bytes\":{allocated_bytes}}}");
}
fn main() {
    let mut memory = Memory::default();
    memory.map(0x1000, PAGE_SIZE * 4, Permissions::RW).unwrap();
    let path = b"/usr/share/vim/vim91/defaults.vim\0";
    memory.write(0x1800, path).unwrap();
    memory.write(0x2ff0, path).unwrap();
    sample("path-resident", 20_000, || {
        let value = memory.read_c_string(black_box(0x1800), 4096).unwrap();
        assert_eq!(black_box(value.as_slice()), &path[..path.len() - 1]);
    });
    sample("path-split", 20_000, || {
        let value = memory.read_c_string(black_box(0x2ff0), 4096).unwrap();
        assert_eq!(black_box(value.as_slice()), &path[..path.len() - 1]);
    });
    sample("empty-resident", 20_000, || {
        assert!(black_box(memory.read_c_string(black_box(0x4000), 4096).unwrap()).is_empty());
    });
    sample("fill-512", 20_000, || {
        assert_eq!(
            memory
                .fill_with(black_box(0x4100), 512, |b| b.fill(7))
                .unwrap()
                .direct_write,
            512
        );
    });
    let code = [
        0xd2800030u32,
        0xf2a00030,
        0xd2800071,
        INSTRUCTION,
        0xd65f03c0,
    ];
    for (i, word) in code.iter().enumerate() {
        memory
            .write(0x1000 + i as u64 * 4, &word.to_le_bytes())
            .unwrap();
    }
    memory.protect(0x1000, PAGE_SIZE, Permissions::RX).unwrap();
    let mut registry = Registry::default();
    sample("native-fill-512", 20_000, || {
        let result = registry
            .invoke(&mut memory, 0x100c, PROFILE, 3, [black_box(0x4100), 8, 512])
            .unwrap();
        assert!(!result.fallback && result.value == 0x4100);
    });
    assert_eq!(memory.read_array::<8>(0x4100).unwrap(), [8; 8]);
}
