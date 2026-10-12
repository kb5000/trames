// SPDX-License-Identifier: GPL-3.0-or-later
//! Reproducible CPU-only profile. Numbers do not represent iPad app performance.
use std::time::Instant;
use trames_cpu::{Backend, Cpu, SliceOutcome};
use trames_memory::{Memory, Permissions, PAGE_SIZE};
fn main() {
    for (name, words, budget) in [
        (
            "integer",
            vec![0x91000400u32, 0xca000021, 0x17fffffe],
            12_000_000u64,
        ),
        (
            "memory",
            vec![0xf9400041u32, 0x91000421, 0xf9000041, 0x17fffffd],
            1_000_000u64,
        ),
    ] {
        let mut expected = None;
        for backend in [Backend::Reference, Backend::Cached, Backend::Cpp] {
            if backend == Backend::Cpp && !Cpu::cpp_available() {
                continue;
            }
            let mut memory = Memory::default();
            memory.map(0x1000, PAGE_SIZE, Permissions::RW).unwrap();
            memory.map(0x4000, PAGE_SIZE, Permissions::RW).unwrap();
            for (k, word) in words.iter().enumerate() {
                memory
                    .write(0x1000 + k as u64 * 4, &word.to_le_bytes())
                    .unwrap();
            }
            memory.protect(0x1000, PAGE_SIZE, Permissions::RX).unwrap();
            let mut cpu = Cpu::new(0x1000, 0x9000);
            cpu.set_x(2, 0x4000);
            cpu.set_backend(backend).unwrap();
            let began = Instant::now();
            let outcome = cpu.run_slice(&mut memory, budget);
            let seconds = began.elapsed().as_secs_f64();
            assert!(matches!(outcome,SliceOutcome::Yield{instructions} if instructions==budget));
            let mut bytes = [0; 8];
            memory.read(0x4000, &mut bytes).unwrap();
            let state = (cpu.x(0), cpu.x(1), cpu.pc, bytes);
            if let Some(expected) = &expected {
                assert_eq!(&state, expected);
            } else {
                expected = Some(state);
            }
            println!("{{\"workload\":\"{name}\",\"backend\":\"{backend:?}\",\"instructions\":{budget},\"seconds\":{seconds},\"instructions_per_second\":{}}}",budget as f64/seconds);
        }
    }
}
