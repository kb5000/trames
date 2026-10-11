// SPDX-License-Identifier: GPL-3.0-or-later
use super::{Cpu, Event, SliceOutcome};
use std::collections::BTreeMap;
use trames_interpreter_cpp::{State, MAX_WORDS};
use trames_memory::Memory;
#[derive(Clone, Debug)]
struct Block {
    words: [u32; MAX_WORDS],
    count: usize,
}
#[derive(Default, Clone, Debug)]
pub(super) struct Cache {
    revision: Option<u64>,
    blocks: BTreeMap<u64, Block>,
}
impl Cpu {
    pub(super) fn run_cpp(&mut self, memory: &mut Memory, budget: u64) -> SliceOutcome {
        let mut executed = 0;
        let mut fallbacks = 0;
        while executed < budget {
            let revision = memory.instruction_revision();
            if self.cache.revision != Some(revision) {
                self.cache.blocks.clear();
                self.cache.revision = Some(revision);
            }
            if !self.cache.blocks.contains_key(&self.pc) {
                if self.cache.blocks.len() >= 256 {
                    self.cache.blocks.clear();
                }
                let mut block = Block {
                    words: [0; MAX_WORDS],
                    count: 0,
                };
                for k in 0..MAX_WORDS {
                    let Some(address) = self.pc.checked_add(k as u64 * 4) else {
                        break;
                    };
                    let Ok(word) = memory.fetch_u32(address) else {
                        break;
                    };
                    block.words[k] = word;
                    block.count += 1;
                }
                // Prefetch faults have no effects. The reference step reports
                // a fault only when that address is actually reached.
                self.cache.blocks.insert(self.pc, block);
            }
            let block = &self.cache.blocks[&self.pc];
            if block.count != 0 {
                let mut state = State {
                    x: self.registers,
                    sp: self.sp,
                    pc: self.pc,
                    nzcv: self.nzcv(),
                    reserved: 0,
                };
                let result = trames_interpreter_cpp::run(
                    &mut state,
                    &block.words[..block.count],
                    (budget - executed).min(4096),
                );
                self.registers = state.x;
                self.sp = state.sp;
                self.pc = state.pc;
                self.set_nzcv(state.nzcv);
                executed += result.instructions;
                if result.reason == 1 {
                    return SliceOutcome::Syscall {
                        instructions: executed,
                    };
                }
                if result.reason == 0 && result.instructions > 0 {
                    continue;
                }
            }
            if executed == budget {
                break;
            }
            fallbacks += 1;
            // Dense memory/SIMD blocks stay in Rust for this slice. Benchmarking
            // showed that copying integer state for every fallback was costly.
            if fallbacks >= 8 {
                return match self.run_reference(memory, budget - executed) {
                    SliceOutcome::Syscall { instructions } => SliceOutcome::Syscall {
                        instructions: executed + instructions,
                    },
                    SliceOutcome::Yield { instructions } => SliceOutcome::Yield {
                        instructions: executed + instructions,
                    },
                    SliceOutcome::Fault(error) => SliceOutcome::Fault(error),
                };
            }
            // Memory, FP, SIMD, unknown/unallocated words and every syscall
            // retain the reference semantics. No effect is retried.
            match self.step(memory) {
                Ok(Event::Continue) => executed += 1,
                Ok(Event::Syscall) => {
                    return SliceOutcome::Syscall {
                        instructions: executed + 1,
                    }
                }
                Err(error) => return SliceOutcome::Fault(error),
            }
        }
        SliceOutcome::Yield {
            instructions: executed,
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    use crate::{Backend, Flags};
    use trames_memory::{Permissions, PAGE_SIZE};
    #[test]
    fn budgets_fallback_flags_aliases_and_cache_invalidation_match_reference() {
        let words = [
            0xd2800060u32,
            0x91000400,
            0xf100041f,
            0x54ffffc1,
            0xd503201f,
            0xd4000001,
        ];
        for budget in [0, 1, 2, 3, 17, 4096, 9000] {
            let mut m = Memory::default();
            m.map(0x1000, PAGE_SIZE, Permissions::RW).unwrap();
            for (k, word) in words.iter().enumerate() {
                m.write(0x1000 + k as u64 * 4, &word.to_le_bytes()).unwrap();
            }
            m.protect(0x1000, PAGE_SIZE, Permissions::RX).unwrap();
            let mut cpu = Cpu::new(0x1000, 0x9000);
            cpu.flags = Flags {
                n: true,
                z: false,
                c: true,
                v: false,
            };
            let mut reference = cpu.clone();
            let mut other = m.clone();
            cpu.set_backend(Backend::Cpp).unwrap();
            let a = cpu.run_slice(&mut m, budget);
            let b = reference.run_slice(&mut other, budget);
            assert_eq!(format!("{a:?}"), format!("{b:?}"));
            assert_eq!(cpu.registers, reference.registers);
            assert_eq!(cpu.pc, reference.pc);
            assert_eq!(cpu.sp, reference.sp);
            assert_eq!(cpu.flags, reference.flags);
            m.protect(0x1000, PAGE_SIZE, Permissions::RW).unwrap();
            m.write(0x1000, &0xd28000e0u32.to_le_bytes()).unwrap();
            m.protect(0x1000, PAGE_SIZE, Permissions::RX).unwrap();
            cpu.pc = 0x1000;
            cpu.run_slice(&mut m, 1);
            assert_eq!(cpu.x(0), 7);
            m.protect(0x1000, PAGE_SIZE, Permissions::R).unwrap();
            cpu.pc = 0x1000;
            assert!(matches!(cpu.run_slice(&mut m, 1), SliceOutcome::Fault(_)));
        }
    }
}
