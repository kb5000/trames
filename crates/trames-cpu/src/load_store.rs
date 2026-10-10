// SPDX-License-Identifier: GPL-3.0-or-later
use super::{sign_extend, Cpu, CpuFault};
use trames_memory::{Memory, MemoryError};

impl Cpu {
    pub(super) fn execute_memory(
        &mut self,
        ins: u32,
        memory: &mut Memory,
    ) -> Result<bool, CpuFault> {
        let pc = self.pc;
        let invalid = || CpuFault::Instruction {
            pc,
            instruction: ins,
        };
        let fault = |error| CpuFault::Memory { pc, error };
        let rt = (ins & 31) as usize;
        let rn = ((ins >> 5) & 31) as usize;
        let vector = ins & (1 << 26) != 0;
        if matches!(ins & 0x3ffffc00, 0x085f7c00 | 0x085ffc00 | 0x08dffc00) {
            // LDXR/LDAXR/LDAR
            let size = 1usize << (ins >> 30);
            let address = self.reg(rn, true);
            if address % size as u64 != 0 {
                return Err(fault(MemoryError::InvalidRange));
            }
            let mut bytes = [0; 8];
            memory.read(address, &mut bytes[..size]).map_err(fault)?;
            self.set_x(rt, u64::from_le_bytes(bytes));
            if ins & 0x3ffffc00 != 0x08dffc00 {
                self.exclusive = Some((address, size, memory.revision()));
            }
            return Ok(true);
        }
        if matches!(ins & 0x3fe0fc00, 0x08007c00 | 0x0800fc00) {
            // STXR/STLXR
            let size = 1usize << (ins >> 30);
            let address = self.reg(rn, true);
            let status = ((ins >> 16) & 31) as usize;
            if status != 31 && (status == rt || status == rn) {
                return Err(invalid());
            }
            if address % size as u64 != 0 {
                return Err(fault(MemoryError::InvalidRange));
            }
            let succeeds = self.exclusive.take() == Some((address, size, memory.revision()));
            if succeeds {
                memory
                    .write(address, &self.x(rt).to_le_bytes()[..size])
                    .map_err(fault)?;
            }
            self.set_x(status, (!succeeds) as u64);
            return Ok(true);
        }
        if ins & 0x3ffffc00 == 0x089ffc00 {
            // STLR
            let size = 1usize << (ins >> 30);
            let address = self.reg(rn, true);
            if address % size as u64 != 0 {
                return Err(fault(MemoryError::InvalidRange));
            }
            memory
                .write(address, &self.x(rt).to_le_bytes()[..size])
                .map_err(fault)?;
            return Ok(true);
        }
        if ins & 0x3a000000 == 0x28000000 {
            // pair loads/stores, all address modes
            let opc = ins >> 30;
            let load = ins & (1 << 22) != 0;
            let size = if vector {
                if opc == 3 {
                    return Err(invalid());
                }
                4usize << opc
            } else {
                match opc {
                    0 => 4,
                    2 => 8,
                    1 if load => 4,
                    _ => return Err(invalid()),
                }
            };
            let rt2 = ((ins >> 10) & 31) as usize;
            let mode = (ins >> 23) & 3;
            let writeback = mode == 1 || mode == 3;
            if !vector
                && ((writeback && rn != 31 && (rn == rt || rn == rt2)) || (load && rt == rt2))
            {
                return Err(invalid());
            }
            let base = self.reg(rn, true);
            let offset = sign_extend(((ins >> 15) & 127) as u64, 7).wrapping_mul(size as u64);
            let address = if mode == 1 {
                base
            } else {
                base.wrapping_add(offset)
            };
            let mut bytes = [0; 32];
            if load {
                memory
                    .read(address, &mut bytes[..size * 2])
                    .map_err(fault)?;
                for (index, reg) in [rt, rt2].into_iter().enumerate() {
                    let mut value = [0; 16];
                    value[..size].copy_from_slice(&bytes[index * size..(index + 1) * size]);
                    if vector {
                        self.vectors[reg] = u128::from_le_bytes(value);
                    } else {
                        let raw = u64::from_le_bytes(value[..8].try_into().expect("integer pair"));
                        self.set_x(reg, if opc == 1 { sign_extend(raw, 32) } else { raw });
                    }
                }
            } else {
                for (index, reg) in [rt, rt2].into_iter().enumerate() {
                    let value = if vector {
                        self.vectors[reg]
                    } else {
                        self.x(reg) as u128
                    };
                    bytes[index * size..(index + 1) * size]
                        .copy_from_slice(&value.to_le_bytes()[..size]);
                }
                memory.write(address, &bytes[..size * 2]).map_err(fault)?;
            }
            if writeback {
                self.sp_or_x(rn, base.wrapping_add(offset));
            }
            return Ok(true);
        }
        let class = ins & 0x3b000000;
        if class == 0x39000000 || class == 0x38000000 {
            // integer/SIMD immediate or register offset
            let size_field = ins >> 30;
            let opc = (ins >> 22) & 3;
            let size = if vector && opc >= 2 {
                if size_field != 0 {
                    return Err(invalid());
                }
                16
            } else {
                1usize << size_field
            };
            let load = if vector { opc & 1 != 0 } else { opc != 0 };
            if !vector && (size == 8 || size == 4) && opc == 3 {
                return Err(invalid());
            }
            let base = self.reg(rn, true);
            let mut writeback = None;
            let address = if class == 0x39000000 {
                base.wrapping_add(((ins >> 10) & 4095) as u64 * size as u64)
            } else if ins & (1 << 21) != 0 {
                if (ins >> 10) & 3 != 2 {
                    return Err(invalid());
                }
                let option = (ins >> 13) & 7;
                let reg = ((ins >> 16) & 31) as usize;
                let index = match option {
                    2 => self.x(reg) as u32 as u64,
                    3 => self.x(reg),
                    6 => self.x(reg) as u32 as i32 as i64 as u64,
                    7 => self.x(reg),
                    _ => return Err(invalid()),
                };
                let shift = if ins & (1 << 12) != 0 {
                    size.trailing_zeros()
                } else {
                    0
                };
                base.wrapping_add(index.wrapping_shl(shift))
            } else {
                let offset = sign_extend(((ins >> 12) & 511) as u64, 9);
                let mode = (ins >> 10) & 3;
                if mode == 2 {
                    return Err(invalid());
                }
                if mode != 0 {
                    if !vector && rn != 31 && rn == rt {
                        return Err(invalid());
                    }
                    writeback = Some(base.wrapping_add(offset));
                }
                if mode == 1 {
                    base
                } else {
                    base.wrapping_add(offset)
                }
            };
            if !vector && size == 8 && opc == 2 {
                return Ok(true);
            } // PRFM hint, no architected memory read
            if load {
                let mut bytes = [0; 16];
                memory.read(address, &mut bytes[..size]).map_err(fault)?;
                if vector {
                    self.vectors[rt] = u128::from_le_bytes(bytes);
                } else {
                    let mut value =
                        u64::from_le_bytes(bytes[..8].try_into().expect("integer load"));
                    if opc >= 2 {
                        value = sign_extend(value, size as u32 * 8);
                    }
                    if opc == 3 {
                        value &= u32::MAX as u64;
                    }
                    self.set_x(rt, value);
                }
            } else {
                let value = if vector {
                    self.vectors[rt]
                } else {
                    self.x(rt) as u128
                };
                memory
                    .write(address, &value.to_le_bytes()[..size])
                    .map_err(fault)?;
            }
            if let Some(value) = writeback {
                self.sp_or_x(rn, value);
            }
            return Ok(true);
        }
        if class == 0x18000000 {
            // literal integer/SIMD loads and prefetch
            let opc = ins >> 30;
            if !vector && opc == 3 {
                return Ok(true);
            }
            let size = if vector {
                if opc == 3 {
                    return Err(invalid());
                }
                4usize << opc
            } else if opc == 1 {
                8
            } else {
                4
            };
            let address = pc.wrapping_add(sign_extend((((ins >> 5) & 0x7ffff) as u64) << 2, 21));
            let mut bytes = [0; 16];
            memory.read(address, &mut bytes[..size]).map_err(fault)?;
            if vector {
                self.vectors[rt] = u128::from_le_bytes(bytes);
            } else {
                let value = u64::from_le_bytes(bytes[..8].try_into().expect("integer literal"));
                self.set_x(
                    rt,
                    if opc == 2 {
                        sign_extend(value, 32)
                    } else {
                        value
                    },
                );
            }
            return Ok(true);
        }
        // Exclusive and acquire/release operations are not silently reduced to normal loads.
        Ok(false)
    }
    fn sp_or_x(&mut self, reg: usize, value: u64) {
        if reg == 31 {
            self.sp = value;
        } else {
            self.set_x(reg, value);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use trames_memory::{Permissions, PAGE_SIZE};
    fn state(instructions: &[u32]) -> (Cpu, Memory) {
        let mut m = Memory::default();
        m.map(0x1000, PAGE_SIZE, Permissions::RW).unwrap();
        for (i, word) in instructions.iter().enumerate() {
            m.write(0x1000 + i as u64 * 4, &word.to_le_bytes()).unwrap();
        }
        m.protect(0x1000, PAGE_SIZE, Permissions::RX).unwrap();
        m.map(0x2000, PAGE_SIZE, Permissions::RW).unwrap();
        (Cpu::new(0x1000, 0x2ff8), m)
    }
    #[test]
    fn pair_access_faults_do_not_partially_change_memory_or_registers() {
        let (mut c, mut m) = state(&[0xa90007e0]); // stp x0,x1,[sp]
        c.set_x(0, 42);
        c.set_x(1, 17);
        m.write(0x2ff8, &9u64.to_le_bytes()).unwrap();
        assert!(c.step(&mut m).is_err());
        assert_eq!(m.read_u64(0x2ff8).unwrap(), 9);
        assert_eq!(c.pc, 0x1000);
        let (mut c, mut m) = state(&[0xa94007e0]); // ldp x0,x1,[sp]
        c.set_x(0, 42);
        c.set_x(1, 17);
        assert!(c.step(&mut m).is_err());
        assert_eq!((c.x(0), c.x(1)), (42, 17));
    }
    #[test]
    fn faulting_post_index_load_preserves_base_register() {
        let (mut c, mut m) = state(&[0xf8408420]); // ldr x0,[x1],#8
        c.set_x(1, 0x3000);
        assert!(c.step(&mut m).is_err());
        assert_eq!(c.x(1), 0x3000);
    }
    #[test]
    fn exclusive_stores_track_reservations_and_memory_mutations() {
        let (mut c, mut m) = state(&[0xc85f7c20, 0xc8027c20]); // ldxr x0,[x1]; stxr w2,x0,[x1]
        c.set_x(1, 0x2000);
        c.step(&mut m).unwrap();
        c.set_x(0, 99);
        c.step(&mut m).unwrap();
        assert_eq!(c.x(2), 0);
        assert_eq!(m.read_u64(0x2000).unwrap(), 99);
        c.pc = 0x1000;
        c.step(&mut m).unwrap();
        m.write(0x2000, &41u64.to_le_bytes()).unwrap();
        c.step(&mut m).unwrap();
        assert_eq!(c.x(2), 1);
        assert_eq!(m.read_u64(0x2000).unwrap(), 41);
        c.pc = 0x1000;
        c.set_x(1, 0x2001);
        assert!(c.step(&mut m).is_err());
    }
}
