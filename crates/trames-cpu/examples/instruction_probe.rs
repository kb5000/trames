// SPDX-License-Identifier: GPL-3.0-or-later
//! Representative decode probes, not ISA conformance or instruction semantics.
//! stdin: identifier + hexadecimal opcode; build with --features cpp.
use std::io::{self, BufRead};
use trames_cpu::{Backend, Cpu, CpuFault, SliceOutcome};
use trames_memory::{Memory, Permissions, PAGE_SIZE};
fn main() {
    for line in io::stdin().lock().lines() {
        let line = line.unwrap();
        let (id, hex) = line.split_once(' ').unwrap();
        let word = u32::from_str_radix(hex, 16).unwrap();
        let mut results = Vec::new();
        for backend in [Backend::Reference, Backend::Cached, Backend::Cpp] {
            let mut memory = Memory::default();
            memory.map(0x1000, PAGE_SIZE, Permissions::RW).unwrap();
            memory.write(0x1000, &word.to_le_bytes()).unwrap();
            memory.protect(0x1000, PAGE_SIZE, Permissions::RX).unwrap();
            memory.map(0x2000, PAGE_SIZE, Permissions::RW).unwrap();
            let mut cpu = Cpu::new(0x1000, 0x2800);
            for reg in 0..31 {
                cpu.set_x(reg, 0x2000);
            }
            cpu.set_backend(backend).unwrap();
            let result = match cpu.run_slice(&mut memory, 1) {
                SliceOutcome::Fault(CpuFault::Instruction { .. }) => "unsupported",
                SliceOutcome::Fault(CpuFault::Memory { .. }) => "decoded-memory-fault",
                SliceOutcome::Syscall { .. } => "syscall",
                SliceOutcome::Yield { .. } => "decoded",
            };
            results.push(result);
        }
        assert!(results.iter().all(|value| value == &results[0]), "{id}");
        println!("{id} {hex} {}", results[0]);
    }
}
