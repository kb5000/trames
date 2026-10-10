// SPDX-License-Identifier: GPL-3.0-or-later
//! Reference AArch64 interpreter. Unsupported encodings fault explicitly.
use std::fmt;
use trames_memory::{Memory, MemoryError};
mod extended;
mod floating;
mod load_store;
mod soft_float;
mod vector_float;

#[derive(Default, Clone, Copy, Debug, PartialEq, Eq)]
pub struct Flags {
    pub n: bool,
    pub z: bool,
    pub c: bool,
    pub v: bool,
}

#[derive(Default, Clone, Debug)]
pub struct Cpu {
    registers: [u64; 31],
    pub sp: u64,
    pub pc: u64,
    pub flags: Flags,
    pub tpidr_el0: u64,
    pub fpcr: u32,
    pub fpsr: u32,
    vectors: [u128; 32],
    exclusive: Option<(u64, usize, u64)>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Event {
    Continue,
    Syscall,
}

#[derive(Debug)]
pub enum CpuFault {
    Memory { pc: u64, error: MemoryError },
    Instruction { pc: u64, instruction: u32 },
}
impl fmt::Display for CpuFault {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Memory { pc, error } => write!(f, "CPU fault at PC {pc:#x}: {error}"),
            Self::Instruction { pc, instruction } => write!(
                f,
                "unsupported or unallocated instruction {instruction:#010x} at PC {pc:#x}"
            ),
        }
    }
}
impl std::error::Error for CpuFault {}

#[derive(Debug)]
pub enum SliceOutcome {
    Syscall { instructions: u64 },
    Yield { instructions: u64 },
    Fault(CpuFault),
}

fn sign_extend(value: u64, bits: u32) -> u64 {
    (((value << (64 - bits)) as i64) >> (64 - bits)) as u64
}
fn width_mask(wide: bool) -> u64 {
    if wide {
        u64::MAX
    } else {
        u32::MAX as u64
    }
}
fn shifted(value: u64, kind: u32, amount: u32, wide: bool) -> u64 {
    let value = value & width_mask(wide);
    (match kind {
        0 => value.wrapping_shl(amount),
        1 => value >> amount,
        2 if wide => ((value as i64) >> amount) as u64,
        2 => ((value as u32 as i32) >> amount) as u32 as u64,
        3 if wide => value.rotate_right(amount),
        3 => (value as u32).rotate_right(amount) as u64,
        _ => unreachable!("two-bit shift"),
    }) & width_mask(wide)
}

impl Cpu {
    pub fn new(pc: u64, sp: u64) -> Self {
        Self {
            pc,
            sp,
            ..Self::default()
        }
    }
    /// Register 31 denotes ZR here; SP is selected explicitly by instruction semantics.
    pub fn x(&self, index: usize) -> u64 {
        if index == 31 {
            0
        } else {
            self.registers[index]
        }
    }
    pub fn set_x(&mut self, index: usize, value: u64) {
        if index != 31 {
            self.registers[index] = value;
        }
    }
    fn reg(&self, index: usize, sp: bool) -> u64 {
        if sp && index == 31 {
            self.sp
        } else {
            self.x(index)
        }
    }
    fn put(&mut self, index: usize, value: u64, wide: bool, sp: bool) {
        let value = value & width_mask(wide);
        if sp && index == 31 {
            self.sp = value;
        } else {
            self.set_x(index, value);
        }
    }
    fn arithmetic(&mut self, a: u64, b: u64, sub: bool, set_flags: bool, wide: bool) -> u64 {
        let mask = width_mask(wide);
        let a = a & mask;
        let b = b & mask;
        let result = if sub {
            a.wrapping_sub(b)
        } else {
            a.wrapping_add(b)
        } & mask;
        if set_flags {
            let sign = if wide { 1 << 63 } else { 1 << 31 };
            self.flags = Flags {
                n: result & sign != 0,
                z: result == 0,
                c: if sub {
                    a >= b
                } else {
                    a as u128 + b as u128 > mask as u128
                },
                v: if sub {
                    (a ^ b) & (a ^ result) & sign != 0
                } else {
                    !(a ^ b) & (a ^ result) & sign != 0
                },
            };
        }
        result
    }
    fn condition(&self, condition: u32) -> bool {
        let f = self.flags;
        let basic = match condition >> 1 {
            0 => f.z,
            1 => f.c,
            2 => f.n,
            3 => f.v,
            4 => f.c && !f.z,
            5 => f.n == f.v,
            6 => !f.z && f.n == f.v,
            7 => true,
            _ => unreachable!(),
        };
        if condition & 1 != 0 && condition != 15 {
            !basic
        } else {
            basic
        }
    }

    pub fn step(&mut self, memory: &mut Memory) -> Result<Event, CpuFault> {
        let pc = self.pc;
        let memory_fault = |error| CpuFault::Memory { pc, error };
        let ins = memory.fetch_u32(pc).map_err(memory_fault)?;
        let invalid = || CpuFault::Instruction {
            pc,
            instruction: ins,
        };
        let rd = (ins & 31) as usize;
        let rn = ((ins >> 5) & 31) as usize;
        let rm = ((ins >> 16) & 31) as usize;
        let wide = ins >> 31 != 0;
        let mut next = pc.wrapping_add(4);
        let mut event = Event::Continue;
        if ins == 0xd503201f { // NOP
        } else if ins == 0xd4000001 {
            // SVC #0; the syscall dispatcher owns guest-visible effects.
            event = Event::Syscall;
        } else if ins & 0x1f800000 == 0x12800000 {
            // MOVN/MOVZ/MOVK
            let opc = (ins >> 29) & 3;
            let shift = ((ins >> 21) & 3) * 16;
            if opc == 1 || (!wide && shift >= 32) {
                return Err(invalid());
            }
            let immediate = (((ins >> 5) & 0xffff) as u64) << shift;
            let value = match opc {
                0 => !immediate,
                2 => immediate,
                3 => (self.x(rd) & !(0xffffu64 << shift)) | immediate,
                _ => unreachable!(),
            };
            self.put(rd, value, wide, false);
        } else if ins & 0x1f000000 == 0x10000000 {
            // ADR/ADRP
            let immediate = sign_extend(
                ((((ins >> 5) & 0x7ffff) as u64) << 2) | ((ins >> 29) & 3) as u64,
                21,
            );
            let value = if wide {
                (pc & !4095).wrapping_add(immediate << 12)
            } else {
                pc.wrapping_add(immediate)
            };
            self.set_x(rd, value);
        } else if ins & 0x1f800000 == 0x11000000 {
            // ADD/SUB immediate
            let sub = ins & (1 << 30) != 0;
            let flags = ins & (1 << 29) != 0;
            let immediate =
                (((ins >> 10) & 0xfff) as u64) << if ins & (1 << 22) != 0 { 12 } else { 0 };
            let value = self.arithmetic(self.reg(rn, true), immediate, sub, flags, wide);
            self.put(rd, value, wide, !flags);
        } else if ins & 0x1f200000 == 0x0b000000 {
            // ADD/SUB shifted register
            let kind = (ins >> 22) & 3;
            let amount = (ins >> 10) & 63;
            if kind == 3 || (!wide && amount >= 32) {
                return Err(invalid());
            }
            let sub = ins & (1 << 30) != 0;
            let flags = ins & (1 << 29) != 0;
            let value = self.arithmetic(
                self.x(rn),
                shifted(self.x(rm), kind, amount, wide),
                sub,
                flags,
                wide,
            );
            self.put(rd, value, wide, false);
        } else if ins & 0x1f000000 == 0x0a000000 {
            // AND/ORR/EOR/ANDS shifted register (+ inverted forms)
            let kind = (ins >> 22) & 3;
            let amount = (ins >> 10) & 63;
            if !wide && amount >= 32 {
                return Err(invalid());
            }
            let mut rhs = shifted(self.x(rm), kind, amount, wide);
            if ins & (1 << 21) != 0 {
                rhs = !rhs;
            }
            let opc = (ins >> 29) & 3;
            let value = match opc {
                0 | 3 => self.x(rn) & rhs,
                1 => self.x(rn) | rhs,
                2 => self.x(rn) ^ rhs,
                _ => unreachable!(),
            } & width_mask(wide);
            if opc == 3 {
                self.flags = Flags {
                    n: value & (if wide { 1 << 63 } else { 1 << 31 }) != 0,
                    z: value == 0,
                    c: false,
                    v: false,
                };
            }
            self.put(rd, value, wide, false);
        } else if ins & 0x7c000000 == 0x14000000 {
            // B/BL
            if wide {
                self.set_x(30, next);
            }
            next = pc.wrapping_add(sign_extend(((ins & 0x03ffffff) as u64) << 2, 28));
        } else if matches!(ins & 0xfffffc1f, 0xd61f0000 | 0xd63f0000 | 0xd65f0000) {
            // BR/BLR/RET
            let target = self.x(rn);
            if ins & 0xfffffc1f == 0xd63f0000 {
                self.set_x(30, next);
            }
            next = target;
        } else if ins & 0x7e000000 == 0x34000000 {
            // CBZ/CBNZ
            let nonzero = ins & (1 << 24) != 0;
            if (self.x(rd) & width_mask(wide) != 0) == nonzero {
                next = pc.wrapping_add(sign_extend((((ins >> 5) & 0x7ffff) as u64) << 2, 21));
            }
        } else if ins & 0xff000010 == 0x54000000 {
            // B.cond
            if self.condition(ins & 15) {
                next = pc.wrapping_add(sign_extend((((ins >> 5) & 0x7ffff) as u64) << 2, 21));
            }
        } else if let Some(target) = self.execute_extended(ins, memory)? {
            next = target;
        } else {
            return Err(invalid());
        }
        self.pc = next;
        Ok(event)
    }

    /// Bounded calls form the execution backend boundary; never a per-instruction FFI call.
    pub fn run_slice(&mut self, memory: &mut Memory, budget: u64) -> SliceOutcome {
        for executed in 0..budget {
            match self.step(memory) {
                Ok(Event::Continue) => {}
                Ok(Event::Syscall) => {
                    return SliceOutcome::Syscall {
                        instructions: executed + 1,
                    }
                }
                Err(e) => return SliceOutcome::Fault(e),
            }
        }
        SliceOutcome::Yield {
            instructions: budget,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use trames_memory::{Permissions, PAGE_SIZE};
    fn code(words: &[u32]) -> Memory {
        let mut m = Memory::default();
        m.map(0x1000, PAGE_SIZE, Permissions::RW).unwrap();
        for (i, word) in words.iter().enumerate() {
            m.write(0x1000 + i as u64 * 4, &word.to_le_bytes()).unwrap();
        }
        m.protect(0x1000, PAGE_SIZE, Permissions::RX).unwrap();
        m
    }
    #[test]
    fn zero_register_writes_and_w_register_zero_extension() {
        let mut m = code(&[0xd280003f, 0x12800000, 0xd503201f]); // mov xzr,1; movn w0,0
        let mut c = Cpu {
            pc: 0x1000,
            ..Cpu::default()
        };
        c.set_x(0, u64::MAX);
        c.step(&mut m).unwrap();
        assert_eq!(c.x(31), 0);
        c.step(&mut m).unwrap();
        assert_eq!(c.x(0), u32::MAX as u64);
    }
    #[test]
    fn arithmetic_flags_cover_carry_and_signed_overflow() {
        let mut c = Cpu::default();
        assert_eq!(c.arithmetic(u64::MAX, 1, false, true, true), 0);
        assert!(c.flags.z && c.flags.c && !c.flags.v);
        assert_eq!(c.arithmetic(0x7fffffff, 1, false, true, false), 0x80000000);
        assert!(c.flags.n && c.flags.v && !c.flags.c);
        c.arithmetic(0, 1, true, true, true);
        assert!(!c.flags.c && c.flags.n);
        c.arithmetic(1 << 63, 1, true, true, true);
        assert!(c.flags.v && c.flags.c);
    }
    #[test]
    fn stack_pointer_and_zero_register_have_distinct_semantics() {
        let mut m = code(&[0x910043ff, 0xf10043ff]); // add sp,sp,16; cmp sp,16
        let mut c = Cpu {
            pc: 0x1000,
            ..Cpu::default()
        };
        c.step(&mut m).unwrap();
        assert_eq!(c.sp, 16);
        c.step(&mut m).unwrap();
        assert!(c.flags.z);
        assert_eq!(c.sp, 16);
    }
    #[test]
    fn branches_syscall_and_budget() {
        let mut m = code(&[0x14000000]);
        let mut c = Cpu {
            pc: 0x1000,
            ..Cpu::default()
        };
        assert!(matches!(
            c.run_slice(&mut m, 5),
            SliceOutcome::Yield { instructions: 5 }
        ));
        assert_eq!(c.pc, 0x1000);
        m = code(&[0xd4000001]);
        assert!(matches!(
            c.run_slice(&mut m, 5),
            SliceOutcome::Syscall { instructions: 1 }
        ));
        assert_eq!(c.pc, 0x1004);
    }
    #[test]
    fn invalid_encodings_and_memory_faults_keep_pc() {
        for instruction in [0, 0x52c00000, 0xd4000021, 0x0bc00000] {
            let mut m = code(&[instruction]);
            let mut c = Cpu {
                pc: 0x1000,
                ..Cpu::default()
            };
            assert!(matches!(c.step(&mut m), Err(CpuFault::Instruction { .. })));
            assert_eq!(c.pc, 0x1000);
        }
        let mut m = code(&[0xf9400020]);
        let mut c = Cpu {
            pc: 0x1000,
            ..Cpu::default()
        };
        c.set_x(1, u64::MAX);
        assert!(matches!(c.step(&mut m), Err(CpuFault::Memory { .. })));
        assert_eq!(c.pc, 0x1000);
    }
}
