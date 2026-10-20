// SPDX-License-Identifier: GPL-3.0-or-later
// Pinned in rust-toolchain.toml: compiler-enforced, bounded-stack dispatch.
#![feature(explicit_tail_calls, rust_preserve_none_cc)]
#![allow(incomplete_features)]
//! AArch64 virtual CPU with a single bounded cached interpreter.
use std::fmt;
use trames_memory::{CpuMemory, MemoryError};
/// Capabilities of the virtual CPU, independent of the host.
pub const HWCAP: u64 = (1 << 0)
    | (1 << 1)
    | (1 << 3)
    | (1 << 4)
    | (1 << 5)
    | (1 << 6)
    | (1 << 7)
    | (1 << 8)
    | (1 << 9)
    | (1 << 10)
    | (1 << 12)
    | (1 << 13)
    | (1 << 15)
    | (1 << 16) // FCMA
    | (1 << 17) // SHA3
    | (1 << 18) // SM3
    | (1 << 19) // SM4
    | (1 << 20)
    | (1 << 21) // SHA512
    | (1 << 23) // FHM
    | (1 << 29) // SB: architectural ordering is provided by the interpreter
    | (1 << 26)
    | (1 << 27);
pub const HWCAP2: u64 =
    (1 << 7) | (1 << 8) | (1 << 13) | (1 << 14) | (1 << 15) | (1 << 16) | (1 << 34) | (1 << 43); // FlagM2, FRINTTS, I8MM, BF16, DGH, RNG, CSSC, MOPS
pub const FEATURE_NAMES: &str = "fp asimd aes pmull sha1 sha2 crc32 atomics fphp asimdhp asimdrdm jscvt lrcpc fcma sha3 sm3 sm4 asimddp sha512 asimdfhm ilrcpc flagm sb flagm2 frint i8mm bf16 dgh rng cssc mops";
mod cached;
mod compatibility;
mod crypto;
mod extended;
mod flags;
mod floating;
mod half_vector;
mod load_store;
mod modern_crypto;
mod modern_memory;
mod modern_scalar;
mod soft_float;
mod vector_extensions;
mod vector_extra_float;
mod vector_float;
mod vector_integer;

#[derive(Default, Clone, Copy, Debug, PartialEq, Eq)]
pub struct Flags {
    pub n: bool,
    pub z: bool,
    pub c: bool,
    pub v: bool,
}

#[derive(Default, Clone, Copy, Debug)]
pub struct DecodeStats {
    pub lookups: u64,
    pub misses: u64,
    pub instructions: u64,
}

#[derive(Default, Clone, Debug)]
pub struct Cpu {
    native_calls: bool,
    vulkan_calls: bool,
    registers: [u64; 31],
    pub sp: u64,
    pub pc: u64,
    condition_flags: flags::ConditionFlags,
    pub tpidr_el0: u64,
    pub tpidr2_el0: u64,
    pub fpcr: u32,
    pub fpsr: u32,
    vectors: [u128; 32],
    exclusive: Option<(u64, usize, u128)>,
    decoded: cached::Cache,
    yield_current: bool,
    fault_retired: u64,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Event {
    Continue,
    Syscall,
    /// Typed virtual native call; PC is held until the dispatcher commits.
    NativeCall {
        pc: u64,
        module: u64,
        function: u64,
    },
    /// A bounded long instruction has made progress but has not retired.
    Yield,
}

#[derive(Debug)]
pub enum CpuFault {
    Memory {
        pc: u64,
        error: MemoryError,
    },
    Instruction {
        pc: u64,
        instruction: u32,
    },
    Trap {
        pc: u64,
        instruction: u32,
        signal: u32,
        code: i32,
    },
}
impl fmt::Display for CpuFault {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Memory { pc, error } => write!(f, "CPU fault at PC {pc:#x}: {error}"),
            Self::Trap {
                pc,
                instruction,
                signal,
                ..
            } => write!(
                f,
                "CPU exception {instruction:#010x} at PC {pc:#x} (signal {signal})"
            ),
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
    Syscall {
        instructions: u64,
    },
    /// Instructions before the pending HLT; it retires only on a committed return.
    NativeCall {
        instructions: u64,
        pc: u64,
        module: u64,
        function: u64,
    },
    Yield {
        instructions: u64,
    },
    Fault(CpuFault),
}

#[inline]
fn sign_extend(value: u64, bits: u32) -> u64 {
    (((value << (64 - bits)) as i64) >> (64 - bits)) as u64
}
#[inline]
fn width_mask(wide: bool) -> u64 {
    if wide {
        u64::MAX
    } else {
        u32::MAX as u64
    }
}
#[inline]
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
    pub fn set_native_calls(&mut self, enabled: bool) {
        self.native_calls = enabled;
    }
    pub fn set_vulkan_calls(&mut self, enabled: bool) {
        self.vulkan_calls = enabled;
    }
    pub fn native_calls_enabled(&self) -> bool {
        self.native_calls || self.vulkan_calls
    }
    pub fn decode_stats(&self) -> DecodeStats {
        self.decoded.stats
    }
    pub fn new(pc: u64, sp: u64) -> Self {
        Self {
            pc,
            sp,
            ..Self::default()
        }
    }
    /// Register 31 denotes ZR here; SP is selected explicitly by instruction semantics.
    #[inline]
    pub fn x(&self, index: usize) -> u64 {
        if index == 31 {
            0
        } else {
            self.registers[index]
        }
    }
    #[inline]
    pub fn set_x(&mut self, index: usize, value: u64) {
        if index != 31 {
            self.registers[index] = value;
        }
    }
    /// Exception entry and virtual CPU migration may clear the local monitor.
    pub fn clear_exclusive(&mut self) {
        self.exclusive = None;
    }
    #[inline]
    fn reg(&self, index: usize, sp: bool) -> u64 {
        if sp && index == 31 {
            self.sp
        } else {
            self.x(index)
        }
    }
    #[inline]
    fn put(&mut self, index: usize, value: u64, wide: bool, sp: bool) {
        let value = value & width_mask(wide);
        if sp && index == 31 {
            self.sp = value;
        } else {
            self.set_x(index, value);
        }
    }
    // The decoder supplies constant flag modes at many call sites. Keep
    // them visible so flag-free ALU operations lose the entire NZCV path.
    #[inline(always)]
    fn arithmetic(&mut self, a: u64, b: u64, sub: bool, set_flags: bool, wide: bool) -> u64 {
        self.condition_flags.calculate(a, b, sub, set_flags, wide)
    }
    #[inline(always)]
    fn condition(&self, condition: u32) -> bool {
        self.condition_flags.condition(condition)
    }

    pub fn step(&mut self, memory: &mut impl CpuMemory) -> Result<Event, CpuFault> {
        let pc = self.pc;
        let memory_fault = |error| CpuFault::Memory { pc, error };
        let ins = memory.fetch_u32(pc).map_err(memory_fault)?;
        let scope = trames_compute::Scope::new(self.fpcr, self.fpsr);
        let result = self.execute_instruction(ins, memory);
        self.fpsr |= trames_compute::status();
        drop(scope);
        result
    }
    fn execute_instruction(
        &mut self,
        ins: u32,
        memory: &mut impl CpuMemory,
    ) -> Result<Event, CpuFault> {
        self.yield_current = false;
        let pc = self.pc;
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
        if (self.native_calls || self.vulkan_calls && self.x(16) == 0x0001_0006)
            && ins == 0xd44a8a40
        {
            next = pc;
            event = Event::NativeCall {
                pc,
                module: self.x(16),
                function: self.x(17),
            };
        } else if matches!(ins & 0xffe0001f, 0xd4200000 | 0xd4400000) {
            return Err(CpuFault::Trap {
                pc,
                instruction: ins,
                signal: if ins & 0xffe0001f == 0xd4200000 { 5 } else { 4 },
                code: 1,
            });
        } else if ins == 0xd503201f { // NOP
        } else if ins & 0xffe0001f == 0xd4000001 {
            // SVC #0; the syscall dispatcher owns guest-visible effects.
            event = Event::Syscall;
        } else if ins & 0x7fe0e000 == 0x1ac04000 {
            // CRC32/CRC32C operate on the little-endian bytes of B/H/W/X.
            let bytes = 1 << ((ins >> 10) & 3);
            if wide != (bytes == 8) {
                return Err(invalid());
            }
            let polynomial = if ins & (1 << 12) != 0 {
                0x82f63b78
            } else {
                0xedb88320
            };
            let native =
                trames_compute::crc(bytes, ins & (1 << 12) != 0, self.x(rn) as u32, self.x(rm));
            let mut crc = self.x(rn) as u32;
            if let Some(value) = native {
                crc = value;
            } else {
                for byte in 0..bytes {
                    crc ^= ((self.x(rm) >> (byte * 8)) & 255) as u32;
                    for _ in 0..8 {
                        crc = (crc >> 1) ^ if crc & 1 != 0 { polynomial } else { 0 };
                    }
                }
            }
            self.set_x(rd, crc as u64);
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
                self.set_flags(Flags {
                    n: value & (if wide { 1 << 63 } else { 1 << 31 }) != 0,
                    z: value == 0,
                    c: false,
                    v: false,
                });
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
        Ok(if std::mem::take(&mut self.yield_current) {
            Event::Yield
        } else {
            event
        })
    }

    /// Completed instructions before the most recent fault, for scheduler
    /// accounting when a signal handler recovers from the failed instruction.
    pub fn fault_retired(&self) -> u64 {
        self.fault_retired
    }
    pub(crate) fn slice_fault(&mut self, error: CpuFault, instructions: u64) -> SliceOutcome {
        self.fault_retired = instructions;
        SliceOutcome::Fault(error)
    }
    pub fn run_slice(&mut self, memory: &mut impl CpuMemory, budget: u64) -> SliceOutcome {
        self.fault_retired = 0;
        let scope = trames_compute::Scope::new(self.fpcr, self.fpsr);
        let result = self.run_cached(&mut *memory.cpu_slice(), budget);
        self.fpsr |= trames_compute::status();
        drop(scope);
        result
    }
    #[cfg(test)]
    fn run_reference(&mut self, memory: &mut impl CpuMemory, budget: u64) -> SliceOutcome {
        for executed in 0..budget {
            match self.step(memory) {
                Ok(Event::Continue) => {}
                Ok(Event::Yield) => {
                    return SliceOutcome::Yield {
                        instructions: executed,
                    }
                }
                Ok(Event::Syscall) => {
                    return SliceOutcome::Syscall {
                        instructions: executed + 1,
                    }
                }
                Ok(Event::NativeCall {
                    pc,
                    module,
                    function,
                }) => {
                    return SliceOutcome::NativeCall {
                        instructions: executed,
                        pc,
                        module,
                        function,
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
use trames_memory::Memory;
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
        assert!(c.flags().z && c.flags().c && !c.flags().v);
        assert_eq!(c.arithmetic(0x7fffffff, 1, false, true, false), 0x80000000);
        assert!(c.flags().n && c.flags().v && !c.flags().c);
        c.arithmetic(0, 1, true, true, true);
        assert!(!c.flags().c && c.flags().n);
        c.arithmetic(1 << 63, 1, true, true, true);
        assert!(c.flags().v && c.flags().c);
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
        assert!(c.flags().z);
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
        for instruction in [0, 0x52c00000, 0xd4000003, 0x0bc00000] {
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

    #[test]
    fn reserved_new_encodings_have_no_architectural_effects() {
        // Invalid RDM widths, scalar vector-only FADD, odd CASP pair, an
        // unallocated atomic operation and the reserved indexed FP type.
        for instruction in [
            0x6e028420, 0x6ec28420, 0x5e421420, 0x48217c82, 0xf8209022, 0x4f729820,
        ] {
            for cached in [false, true] {
                let mut memory = code(&[instruction]);
                memory.map(0x2000, PAGE_SIZE, Permissions::RW).unwrap();
                memory.write(0x2000, &[0x5a; 32]).unwrap();
                let mut cpu = Cpu::new(0x1000, 0x2800);
                for i in 0..31 {
                    cpu.set_x(i, 0x2000);
                }
                for i in 0..32 {
                    cpu.set_vector(i, u128::MAX);
                }
                cpu.set_nzcv(9);
                cpu.fpcr = 0x07c80000;
                cpu.fpsr = 0x9f;
                let before = cpu.clone();
                let revision = memory.revision();
                assert!(
                    matches!(
                        if cached {
                            cpu.run_slice(&mut memory, 1)
                        } else {
                            cpu.run_reference(&mut memory, 1)
                        },
                        SliceOutcome::Fault(CpuFault::Instruction { .. })
                    ),
                    "{instruction:08x} cached={cached}"
                );
                assert_eq!(cpu.registers, before.registers);
                assert_eq!(cpu.vectors, before.vectors);
                assert_eq!(
                    (
                        cpu.pc,
                        cpu.sp,
                        cpu.nzcv(),
                        cpu.fpcr,
                        cpu.fpsr,
                        cpu.exclusive
                    ),
                    (
                        before.pc,
                        before.sp,
                        before.nzcv(),
                        before.fpcr,
                        before.fpsr,
                        before.exclusive
                    )
                );
                assert_eq!(memory.revision(), revision);
            }
        }
    }

    #[test]
    fn failed_atomic_write_preflight_keeps_memory_and_registers() {
        for instruction in [0xf8208022, 0x48207c82] {
            // SWP / CASP
            for cached in [false, true] {
                let mut memory = code(&[instruction]);
                memory.map(0x2000, PAGE_SIZE, Permissions::RW).unwrap();
                memory.write(0x2000, &[0xa5; 16]).unwrap();
                memory.protect(0x2000, PAGE_SIZE, Permissions::R).unwrap();
                let mut cpu = Cpu::new(0x1000, 0x2800);
                for i in 0..31 {
                    cpu.set_x(i, 0x2000);
                }
                let before = cpu.clone();
                let revision = memory.revision();
                assert!(matches!(
                    if cached {
                        cpu.run_slice(&mut memory, 1)
                    } else {
                        cpu.run_reference(&mut memory, 1)
                    },
                    SliceOutcome::Fault(CpuFault::Memory { .. })
                ));
                assert_eq!(cpu.registers, before.registers);
                assert_eq!(cpu.pc, before.pc);
                assert_eq!(memory.revision(), revision);
                let mut contents = [0; 16];
                memory.read(0x2000, &mut contents).unwrap();
                assert_eq!(contents, [0xa5; 16]);
            }
        }
    }

    #[test]
    fn serial_exclusive_store_succeeds_after_an_unrelated_stack_spill() {
        // The original Firefox 142 lock loop spills its desired CAS value
        // between LDAXR and STLXR. The spill must not force every retry to fail.
        let mut memory = code(&[0xc85ffee9, 0xf90023eb, 0xc80afeeb]);
        memory.map(0x2000, PAGE_SIZE, Permissions::RW).unwrap();
        memory.map(0x4000, PAGE_SIZE, Permissions::RW).unwrap();
        memory.write_array(0x2000, 1u64.to_le_bytes()).unwrap();
        let mut cpu = Cpu::new(0x1000, 0x4000);
        cpu.set_x(23, 0x2000);
        cpu.set_x(11, 2);
        for _ in 0..3 {
            cpu.step(&mut memory).unwrap();
        }
        assert_eq!(cpu.x(9), 1);
        assert_eq!(cpu.x(10), 0);
        assert_eq!(memory.read_u64(0x2000).unwrap(), 2);
        assert_eq!(memory.read_u64(0x4040).unwrap(), 2);
    }

    #[test]
    fn rcpc_load_does_not_create_an_exclusive_reservation() {
        let mut memory = code(&[0xf8bfc020, 0xc8037c24]); // LDAPR X0 / STXR W3,X4
        memory.map(0x2000, PAGE_SIZE, Permissions::RW).unwrap();
        memory.write(0x2000, &7u64.to_le_bytes()).unwrap();
        let mut cpu = Cpu::new(0x1000, 0x2800);
        cpu.set_x(1, 0x2000);
        cpu.set_x(4, 99);
        cpu.step(&mut memory).unwrap();
        assert_eq!(cpu.x(0), 7);
        cpu.step(&mut memory).unwrap();
        assert_eq!(cpu.x(3), 1);
        assert_eq!(memory.read_u64(0x2000).unwrap(), 7);
    }

    #[test]
    fn virtual_counter_and_cache_maintenance_preserve_guest_state() {
        let mut memory = code(&[0xd53be000, 0xd53be041, 0xd53be042]);
        let mut cpu = Cpu::new(0x1000, 0x2800);
        cpu.step(&mut memory).unwrap();
        cpu.step(&mut memory).unwrap();
        cpu.step(&mut memory).unwrap();
        assert_eq!(cpu.x(0), 1_000_000_000);
        assert!(cpu.x(2) >= cpu.x(1));
        for instruction in [0xd50b7b21, 0xd50b7a21, 0xd50b7e21, 0xd50b7521] {
            memory = code(&[instruction]);
            memory
                .map(
                    0x2000,
                    PAGE_SIZE,
                    Permissions {
                        read: false,
                        write: false,
                        execute: false,
                    },
                )
                .unwrap();
            cpu = Cpu::new(0x1000, 0x2800);
            cpu.set_x(1, 0x2001);
            let revision = memory.instruction_revision();
            cpu.step(&mut memory).unwrap();
            assert_eq!(memory.instruction_revision(), revision);
            cpu.pc = 0x1000;
            cpu.set_x(1, 0x3000);
            assert!(matches!(
                cpu.step(&mut memory),
                Err(CpuFault::Memory { .. })
            ));
            assert_eq!(cpu.pc, 0x1000);
        }
    }
}

#[cfg(test)]
mod native_tests {
    use super::*;
    use trames_memory::{Permissions, PAGE_SIZE};
    #[test]
    fn vulkan_opt_in_does_not_activate_the_pure_memory_profile() {
        let mut m = Memory::default();
        m.map(0x1000, PAGE_SIZE, Permissions::RW).unwrap();
        m.write(0x1000, &0xd44a8a40u32.to_le_bytes()).unwrap();
        m.protect(0x1000, PAGE_SIZE, Permissions::RX).unwrap();
        let mut gpu = Cpu::new(0x1000, 0);
        gpu.set_vulkan_calls(true);
        gpu.set_x(16, 0x10006);
        gpu.set_x(17, 1);
        assert!(matches!(
            gpu.run_slice(&mut m, 1),
            SliceOutcome::NativeCall {
                module: 0x10006,
                ..
            }
        ));
        let mut pure = Cpu::new(0x1000, 0);
        pure.set_vulkan_calls(true);
        pure.set_x(16, 0x10001);
        pure.set_x(17, 1);
        assert!(matches!(
            pure.run_slice(&mut m, 1),
            SliceOutcome::Fault(CpuFault::Trap { signal: 4, .. })
        ));
    }
    #[test]
    fn native_exit_gating_pc_and_budget_match_reference() {
        let mut m = Memory::default();
        m.map(0x1000, PAGE_SIZE, Permissions::RW).unwrap();
        m.write(0x1000, &0xd503201fu32.to_le_bytes()).unwrap();
        m.write(0x1004, &0xd44a8a40u32.to_le_bytes()).unwrap();
        m.protect(0x1000, PAGE_SIZE, Permissions::RX).unwrap();
        let mut disabled = Cpu::new(0x1004, 0);
        assert!(matches!(
            disabled.run_slice(&mut m, 1),
            SliceOutcome::Fault(CpuFault::Trap {
                pc: 0x1004,
                signal: 4,
                ..
            })
        ));
        for budget in [0, 1, 2, 3] {
            let mut c = Cpu::new(0x1000, 0);
            c.set_native_calls(true);
            c.set_x(16, 0x10001);
            c.set_x(17, 3);
            let mut reference = c.clone();
            let mut reference_memory = m.clone();
            let a = c.run_slice(&mut m, budget);
            let b = reference.run_reference(&mut reference_memory, budget);
            assert_eq!(format!("{a:?}"), format!("{b:?}"));
            assert_eq!(c.pc, reference.pc);
            if budget >= 2 {
                assert!(matches!(
                    a,
                    SliceOutcome::NativeCall {
                        instructions: 1,
                        pc: 0x1004,
                        module: 0x10001,
                        function: 3
                    }
                ));
            }
        }
        let mut c = Cpu::new(0x1004, 0);
        c.set_native_calls(true);
        assert!(matches!(
            c.step(&mut m),
            Ok(Event::NativeCall { pc: 0x1004, .. })
        ));
        assert_eq!(c.pc, 0x1004);
        // Every other HLT and every BRK retains its normal architectural fault.
        m.protect(0x1000, PAGE_SIZE, Permissions::RW).unwrap();
        m.write(0x1004, &0xd4400000u32.to_le_bytes()).unwrap();
        m.protect(0x1000, PAGE_SIZE, Permissions::RX).unwrap();
        assert!(matches!(
            c.run_slice(&mut m, 1),
            SliceOutcome::Fault(CpuFault::Trap { signal: 4, .. })
        ));
    }
}
