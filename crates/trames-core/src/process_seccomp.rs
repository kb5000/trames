// SPDX-License-Identifier: GPL-3.0-or-later
//! Guest classic-BPF seccomp. Immutable programs run only at syscall entry;
//! filters never reach the host kernel and add no CPU/memory hot-path work.
use super::*;

const ALLOW: u32 = 0x7fff_0000;
const KILL_PROCESS: u32 = 0x8000_0000;
const ARCH: u32 = 0xc000_00b7; // AUDIT_ARCH_AARCH64
const MAX_INSNS: usize = 4096;
const MAX_PATH: usize = 32768;

#[derive(Clone, Copy)]
struct Instruction {
    code: u16,
    yes: u8,
    no: u8,
    value: u32,
}
struct Filter(Vec<Instruction>);
#[derive(Default, Clone)]
pub(super) struct Seccomp {
    strict: bool,
    filters: Vec<Rc<Filter>>,
}
impl Seccomp {
    pub fn mode(&self) -> u32 {
        if self.strict {
            1
        } else if self.filters.is_empty() {
            0
        } else {
            2
        }
    }
    pub fn count(&self) -> usize {
        self.filters.len()
    }
    fn ancestor_of(&self, other: &Self) -> bool {
        !self.strict
            && self.filters.len() <= other.filters.len()
            && self
                .filters
                .iter()
                .zip(&other.filters)
                .all(|(a, b)| Rc::ptr_eq(a, b))
    }
    fn decision(&self, number: u32, pc: u64, args: [u64; 6]) -> u32 {
        if self.strict {
            return if matches!(number, 63 | 64 | 93 | 139) {
                ALLOW
            } else {
                0
            };
        }
        let mut input = [0u32; 16];
        input[0] = number;
        input[1] = ARCH;
        input[2] = pc as u32;
        input[3] = (pc >> 32) as u32;
        for (i, a) in args.into_iter().enumerate() {
            input[4 + i * 2] = a as u32;
            input[5 + i * 2] = (a >> 32) as u32;
        }
        self.filters.iter().rev().fold(ALLOW, |old, filter| {
            let new = filter.run(&input);
            if ((new & 0xffff_0000) as i32) < ((old & 0xffff_0000) as i32) {
                new
            } else {
                old
            }
        })
    }
}
impl Filter {
    fn load(memory: &Memory, address: u64) -> Result<Self, Errno> {
        Linux::check(memory, address, 16, Access::Read)?;
        let mut header = [0; 16];
        memory.read(address, &mut header).map_err(|_| Errno(14))?;
        let n = u16::from_le_bytes(header[..2].try_into().unwrap()) as usize;
        if n == 0 || n > MAX_INSNS {
            return Err(Errno(22));
        }
        let pointer = u64::from_le_bytes(header[8..16].try_into().unwrap());
        Linux::check(memory, pointer, n * 8, Access::Read)?;
        let mut bytes = vec![0; n * 8];
        memory.read(pointer, &mut bytes).map_err(|_| Errno(14))?;
        let code: Vec<_> = bytes
            .chunks_exact(8)
            .map(|i| Instruction {
                code: u16::from_le_bytes(i[..2].try_into().unwrap()),
                yes: i[2],
                no: i[3],
                value: u32::from_le_bytes(i[4..8].try_into().unwrap()),
            })
            .collect();
        Self::validate(code)
    }
    fn validate(code: Vec<Instruction>) -> Result<Self, Errno> {
        if code.is_empty()
            || code.len() > MAX_INSNS
            || !matches!(code.last().unwrap().code, 0x06 | 0x16)
        {
            return Err(Errno(22));
        }
        let mut initialized = vec![None; code.len()];
        initialized[0] = Some(0u16);
        for (pc, i) in code.iter().enumerate() {
            let mut mask = initialized[pc];
            let mut targets = vec![pc + 1];
            match i.code {
                0x00 | 0x01 | 0x80 | 0x81 | 0x07 | 0x87 => {}
                0x20 if i.value < 64 && i.value.is_multiple_of(4) => {}
                0x60 | 0x61 if i.value < 16 => {
                    if mask.is_some_and(|m| m & (1 << i.value) == 0) {
                        return Err(Errno(22));
                    }
                }
                0x02 | 0x03 if i.value < 16 => {
                    mask = mask.map(|m| m | (1 << i.value));
                }
                0x04 | 0x0c | 0x14 | 0x1c | 0x24 | 0x2c | 0x4c | 0x44 | 0x54 | 0x5c | 0x6c
                | 0x7c | 0x84 | 0xac | 0xa4 | 0x3c | 0x9c => {}
                0x34 | 0x94 if i.value != 0 => {}
                0x64 | 0x74 if i.value < 32 => {}
                0x05 => {
                    targets = vec![pc
                        .checked_add(i.value as usize)
                        .and_then(|n| n.checked_add(1))
                        .ok_or(Errno(22))?];
                }
                0x15 | 0x1d | 0x25 | 0x2d | 0x35 | 0x3d | 0x45 | 0x4d => {
                    targets = vec![pc + 1 + i.yes as usize, pc + 1 + i.no as usize];
                }
                0x06 | 0x16 => targets.clear(),
                _ => return Err(Errno(22)),
            }
            for target in targets {
                if target >= code.len() {
                    return Err(Errno(22));
                }
                if let Some(mask) = mask {
                    initialized[target] = Some(initialized[target].map_or(mask, |old| old & mask));
                }
            }
        }
        Ok(Self(code))
    }
    fn run(&self, input: &[u32; 16]) -> u32 {
        let (mut a, mut x, mut pc) = (0u32, 0u32, 0usize);
        let mut scratch = [0u32; 16];
        while let Some(i) = self.0.get(pc) {
            pc += 1;
            match i.code {
                0x00 => a = i.value,
                0x01 => x = i.value,
                0x20 => a = input[(i.value / 4) as usize],
                0x80 => a = 64,
                0x81 => x = 64,
                0x60 => a = scratch[i.value as usize],
                0x61 => x = scratch[i.value as usize],
                0x02 => scratch[i.value as usize] = a,
                0x03 => scratch[i.value as usize] = x,
                0x07 => x = a,
                0x87 => a = x,
                0x06 => return i.value,
                0x16 => return a,
                0x05 => pc += i.value as usize,
                c if c & 7 == 4 => {
                    let v = if c & 8 != 0 { x } else { i.value };
                    a = match c & 0xf0 {
                        0x00 => a.wrapping_add(v),
                        0x10 => a.wrapping_sub(v),
                        0x20 => a.wrapping_mul(v),
                        0x30 => {
                            if v == 0 {
                                return 0;
                            } else {
                                a / v
                            }
                        }
                        0x40 => a | v,
                        0x50 => a & v,
                        0x60 => a.checked_shl(v).unwrap_or(0),
                        0x70 => a.checked_shr(v).unwrap_or(0),
                        0x80 => a.wrapping_neg(),
                        0x90 => {
                            if v == 0 {
                                return 0;
                            } else {
                                a % v
                            }
                        }
                        0xa0 => a ^ v,
                        _ => return KILL_PROCESS,
                    };
                }
                c if c & 7 == 5 => {
                    let v = if c & 8 != 0 { x } else { i.value };
                    let yes = match c & 0xf0 {
                        0x10 => a == v,
                        0x20 => a > v,
                        0x30 => a >= v,
                        0x40 => a & v != 0,
                        _ => false,
                    };
                    pc += if yes { i.yes } else { i.no } as usize;
                }
                _ => return KILL_PROCESS,
            }
        }
        KILL_PROCESS
    }
}
impl Kernel {
    pub(super) fn seccomp(&mut self, p: &mut Process, a: [u64; 6]) -> Result<u64, Errno> {
        match a[0] {
            0 => {
                if a[1] != 0 || a[2] != 0 || p.seccomp.mode() != 0 {
                    return Err(Errno(22));
                }
                p.seccomp.strict = true;
                Ok(0)
            }
            1 => {
                // TSYNC supported. Other installation modes fail explicitly;
                // in particular no fake notification listener FD is returned.
                if a[1] & !1 != 0 || p.seccomp.strict {
                    return Err(Errno(22));
                }
                if !p.linux.vfs.credentials.caps.no_new_privs
                    && !p.linux.vfs.credentials.has_cap(capabilities::SYS_ADMIN)
                {
                    return Err(Errno(13));
                }
                let filter = Rc::new(Filter::load(&p.memory, a[2])?);
                if p.seccomp
                    .filters
                    .iter()
                    .map(|f| f.0.len() + 4)
                    .sum::<usize>()
                    + filter.0.len()
                    > MAX_PATH
                {
                    return Err(Errno(12));
                }
                if a[1] & 1 != 0 {
                    if let Some((&id, _)) = self.processes.iter().find(|(_, t)| {
                        t.tgid == p.tgid
                            && !matches!(t.state, State::Zombie(_))
                            && !t.seccomp.ancestor_of(&p.seccomp)
                    }) {
                        return Ok(p.linux.vfs.visible_pid(id) as u64);
                    }
                }
                p.seccomp.filters.push(filter);
                if a[1] & 1 != 0 {
                    for t in self
                        .processes
                        .values_mut()
                        .filter(|t| t.tgid == p.tgid && !matches!(t.state, State::Zombie(_)))
                    {
                        t.seccomp = p.seccomp.clone();
                        t.linux.vfs.credentials.caps.no_new_privs |=
                            p.linux.vfs.credentials.caps.no_new_privs;
                    }
                }
                Ok(0)
            }
            2 => {
                if a[1] != 0 {
                    return Err(Errno(22));
                }
                let action = read_u32(&p.memory, a[2])?;
                if matches!(
                    action,
                    0 | KILL_PROCESS | 0x0003_0000 | 0x0005_0000 | 0x7ff0_0000 | ALLOW
                ) {
                    Ok(0)
                } else {
                    Err(Errno(95))
                }
            }
            _ => Err(Errno(22)),
        }
    }
    /// True means the syscall was consumed without executing its implementation.
    pub(super) fn filter_syscall(
        &mut self,
        pid: u32,
        p: &mut Process,
        number: u32,
        args: [u64; 6],
    ) -> bool {
        let decision = p.seccomp.decision(number, p.cpu.pc, args);
        match decision & 0xffff_0000 {
            ALLOW => return false,
            0x0005_0000 => {
                p.cpu.set_x(0, error(Errno((decision & 0xffff).min(4095))));
                p.state = State::Ready;
            }
            0x7ff0_0000 | 0x7fc0_0000 => {
                p.cpu.set_x(0, error(Errno(38)));
                p.state = State::Ready;
            }
            0x0003_0000 => {
                // force_sig_seccomp cannot be suppressed by ignoring or
                // blocking SIGSYS; Linux forces its fatal default action.
                if p.mask & (1u64 << 30) != 0 || p.actions[31].handler == 1 {
                    self.exit(pid, p, 31);
                    return true;
                }
                p.cpu.set_x(0, number as u64);
                p.state = State::Ready;
                p.pending.discard(31);
                let _ = p.pending.enqueue(
                    31,
                    Info {
                        sender: decision & 0xffff,
                        uid: ARCH,
                        status: number as i32,
                        code: 1,
                        timer: None,
                        fault: Some(p.cpu.pc),
                    },
                );
                if self.deliver(pid, p).is_err() {
                    self.exit(pid, p, 31);
                }
            }
            0 => self.exit_one(pid, p, 31),
            _ => self.exit(pid, p, 31), // Unknown actions fail closed, as Linux does.
        }
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn ignored_or_blocked_sigsys_cannot_suppress_a_forced_seccomp_trap() {
        for (handler, mask) in [(1, 0), (0x5000, 1u64 << 30)] {
            let mut p = super::super::tests::process();
            let mut kernel = super::super::tests::kernel();
            p.seccomp.filters.push(Rc::new(
                Filter::validate(vec![i(6, 0, 0, 0x30055)]).unwrap(),
            ));
            p.actions[31].handler = handler;
            p.mask = mask;
            assert!(kernel.filter_syscall(1, &mut p, 173, [0; 6]));
            assert!(matches!(p.state, State::Zombie(31)));
        }
    }
    fn i(code: u16, yes: u8, no: u8, value: u32) -> Instruction {
        Instruction {
            code,
            yes,
            no,
            value,
        }
    }
    #[test]
    fn filter_loads_arch_arguments_and_verifies_all_paths() {
        let f = Filter::validate(vec![
            i(0x20, 0, 0, 4),
            i(0x15, 1, 0, ARCH),
            i(0x06, 0, 0, KILL_PROCESS),
            i(0x20, 0, 0, 20),
            i(0x15, 0, 1, 7),
            i(0x06, 0, 0, 0x50001),
            i(0x06, 0, 0, ALLOW),
        ])
        .unwrap();
        let mut input = [0; 16];
        input[1] = ARCH;
        input[5] = 7;
        assert_eq!(f.run(&input), 0x50001);
        input[5] = 8;
        assert_eq!(f.run(&input), ALLOW);
        input[1] = 0;
        assert_eq!(f.run(&input), KILL_PROCESS);
        for bad in [
            vec![i(0x20, 0, 0, 64), i(6, 0, 0, ALLOW)],
            vec![i(0x05, 0, 0, u32::MAX), i(6, 0, 0, ALLOW)],
            vec![i(0x60, 0, 0, 0), i(6, 0, 0, ALLOW)],
            vec![i(0x34, 0, 0, 0), i(6, 0, 0, ALLOW)],
            vec![
                i(0x15, 0, 1, 1),
                i(2, 0, 0, 0),
                i(0x60, 0, 0, 0),
                i(6, 0, 0, ALLOW),
            ],
        ] {
            assert!(Filter::validate(bad).is_err());
        }
    }
    #[test]
    fn stacked_filters_keep_restrictions_and_newest_equal_action_data() {
        let a = Rc::new(Filter::validate(vec![i(6, 0, 0, 0x50001)]).unwrap());
        let b = Rc::new(Filter::validate(vec![i(6, 0, 0, 0x50002)]).unwrap());
        let ancestor = Seccomp {
            strict: false,
            filters: vec![a.clone()],
        };
        let current = Seccomp {
            strict: false,
            filters: vec![a, b],
        };
        assert!(ancestor.ancestor_of(&current));
        assert!(!current.ancestor_of(&ancestor));
        assert_eq!(current.decision(172, 0, [0; 6]), 0x50002);
    }
}
