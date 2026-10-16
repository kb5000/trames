// SPDX-License-Identifier: GPL-3.0-or-later
use trames_cpu::{Cpu, SliceOutcome};
use trames_memory::{Memory, Permissions, PAGE_SIZE};

#[test]
fn millions_of_tail_transitions_fit_in_a_small_host_stack() {
    std::thread::Builder::new()
        .stack_size(256 * 1024)
        .spawn(|| {
            let mut memory = Memory::default();
            memory.map(0x1000, PAGE_SIZE, Permissions::RW).unwrap();
            let words = [
                0x91000400u32, // add x0, x0, #1
                0xf1000421,    // subs x1, x1, #1
                0x54ffffc1,    // b.ne -8
                0xd4000001,    // svc #0
            ];
            for (i, word) in words.iter().enumerate() {
                memory
                    .write_array(0x1000 + i as u64 * 4, word.to_le_bytes())
                    .unwrap();
            }
            memory.protect(0x1000, PAGE_SIZE, Permissions::RX).unwrap();
            let mut cpu = Cpu::new(0x1000, 0);
            cpu.set_x(1, 1_000_000);
            assert!(matches!(
                cpu.run_slice(&mut memory, 3_000_001),
                SliceOutcome::Syscall {
                    instructions: 3_000_001
                }
            ));
            assert_eq!(cpu.x(0), 1_000_000);
            assert_eq!(cpu.x(1), 0);
            assert_eq!(cpu.pc, 0x1010);
            assert_eq!(cpu.nzcv(), 6);
        })
        .unwrap()
        .join()
        .unwrap();
}
