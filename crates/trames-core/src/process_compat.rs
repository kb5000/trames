// SPDX-License-Identifier: GPL-3.0-or-later
//! Compatibility state belongs to virtual tasks, never the host process.
use super::*;

const SS_DISABLE: u32 = 2;
const SS_AUTODISARM: u32 = 0x8000_0000;

#[derive(Clone, Copy)]
pub(super) struct AltStack {
    pub sp: u64,
    pub size: u64,
    pub flags: u32,
}

impl Default for AltStack {
    fn default() -> Self {
        Self {
            sp: 0,
            size: 0,
            flags: SS_DISABLE,
        }
    }
}
impl AltStack {
    pub fn contains(self, sp: u64) -> bool {
        self.flags & SS_DISABLE == 0 && sp > self.sp && sp.wrapping_sub(self.sp) <= self.size
    }
    pub fn on_stack(self, sp: u64) -> bool {
        self.flags & SS_AUTODISARM == 0 && self.contains(sp)
    }
    pub fn reported_flags(self, sp: u64) -> u32 {
        self.flags | u32::from(self.on_stack(sp))
    }
    fn parse(memory: &Memory, address: u64) -> Result<Self, Errno> {
        Linux::check(memory, address, 24, Access::Read)?;
        let flags = read_u32(memory, address + 8)?;
        // Linux accepts SS_ONSTACK on input as well as in saved ucontexts.
        if !matches!(flags & !SS_AUTODISARM, 0 | 1 | SS_DISABLE) {
            return Err(Errno(22));
        }
        let flags = flags & !1;
        if flags & SS_DISABLE != 0 {
            return Ok(Self {
                flags,
                ..Self::default()
            });
        }
        let size = read_u64(memory, address + 16)?;
        if size < 2048 {
            return Err(Errno(12));
        }
        Ok(Self {
            sp: read_u64(memory, address)?,
            size,
            flags,
        })
    }
    pub fn restore(memory: &Memory, address: u64) -> Result<Self, Errno> {
        Self::parse(memory, address)
    }
}

impl Kernel {
    pub(super) fn sigaltstack(p: &mut Process, new: u64, old: u64) -> Result<u64, Errno> {
        if old != 0 {
            Linux::check(&p.memory, old, 24, Access::Write)?;
        }
        let next = if new != 0 {
            let next = AltStack::parse(&p.memory, new)?;
            if p.alt_stack.on_stack(p.cpu.sp) {
                return Err(Errno(1));
            }
            Some(next)
        } else {
            None
        };
        if old != 0 {
            let mut bytes = [0; 24];
            bytes[..8].copy_from_slice(&p.alt_stack.sp.to_le_bytes());
            bytes[8..12].copy_from_slice(&p.alt_stack.reported_flags(p.cpu.sp).to_le_bytes());
            bytes[16..24].copy_from_slice(&p.alt_stack.size.to_le_bytes());
            p.memory.write(old, &bytes).map_err(|_| Errno(14))?;
        }
        if let Some(next) = next {
            p.alt_stack = next;
        }
        Ok(0)
    }
    pub(super) fn membarrier(p: &Process, command: u64, flags: u64) -> Result<u64, Errno> {
        if flags != 0 {
            return Err(Errno(22));
        }
        // Every CPU has returned before coordinator syscalls. The joined
        // checkpoint and host fence order guest, backing and native accesses.
        match command {
            0 => Ok(1 | 8 | 16 | 512),
            16 => {
                p.membarrier_registered.set(true);
                Ok(0)
            }
            512 => Ok(if p.membarrier_registered.get() { 16 } else { 0 }),
            8 if !p.membarrier_registered.get() => Err(Errno(1)),
            1 | 8 => {
                std::sync::atomic::fence(std::sync::atomic::Ordering::SeqCst);
                Ok(0)
            }
            _ => Err(Errno(22)),
        }
    }
    pub(super) fn ioprio_get(&self, pid: u32, which: u64, who: u64) -> Result<u64, Errno> {
        if !(1..=3).contains(&which) || (who as i32) < 0 {
            return Err(Errno(22));
        }
        if which != 1 {
            return Err(Errno(95));
        }
        let target = who as u32;
        if target == 0 || target == pid || self.processes.contains_key(&target) {
            // No explicit I/O priority is installed (CLASS_NONE, data 0).
            // This describes the virtual scheduler; no host PID is consulted.
            Ok(0)
        } else {
            Err(Errno(3))
        }
    }
    pub(super) fn rlimit(p: &mut Process, number: u32, a: [u64; 6]) -> Result<u64, Errno> {
        let (resource, new, old) = if number == 261 {
            if a[0] != 0 && a[0] != p.tgid as u64 {
                return Err(Errno(3));
            }
            (a[1], a[2], a[3])
        } else {
            (
                a[0],
                if number == 164 { a[1] } else { 0 },
                if number == 163 { a[1] } else { 0 },
            )
        };
        let fixed = match resource {
            0 | 1 | 10 | 15 => u64::MAX,
            2 | 5 | 9 => trames_memory::DEFAULT_MEMORY_LIMIT as u64,
            3 => super::super::STACK_SIZE,
            4 | 8 | 12 | 13 | 14 => 0,
            6 => MAX_PROCESSES as u64,
            7 => trames_vfs::MAX_FDS as u64,
            11 => 64,
            _ => return Err(Errno(22)),
        };
        let previous = if resource == 1 {
            p.file_size_limit.get()
        } else {
            (fixed, fixed)
        };
        if old != 0 {
            Linux::check(&p.memory, old, 16, Access::Write)?;
        }
        let next = if new != 0 {
            Linux::check(&p.memory, new, 16, Access::Read)?;
            let limits = (read_u64(&p.memory, new)?, read_u64(&p.memory, new + 8)?);
            if limits.0 > limits.1 {
                return Err(Errno(22));
            }
            if limits.1 > previous.1 && p.linux.vfs.credentials.euid != 0 {
                return Err(Errno(1));
            }
            if resource != 1 && limits.1 > fixed {
                return Err(Errno(1));
            }
            if resource != 1 && limits != previous {
                return Err(Errno(95));
            }
            Some(limits)
        } else {
            None
        };
        if old != 0 {
            write_u64(&mut p.memory, old, previous.0)?;
            write_u64(&mut p.memory, old + 8, previous.1)?;
        }
        if let Some(next) = next {
            if resource == 1 {
                p.file_size_limit.set(next);
            }
        }
        Ok(0)
    }
}

#[cfg(test)]
mod tests {
    use super::super::tests::{kernel, process};
    use super::*;
    #[test]
    fn compatibility_state_and_fault_preflight() {
        let mut p = process();
        write_u64(&mut p.memory, 0x1000, 0x2000).unwrap();
        write_u64(&mut p.memory, 0x1008, 0).unwrap();
        write_u64(&mut p.memory, 0x1010, 2048).unwrap();
        assert_eq!(Kernel::sigaltstack(&mut p, 0x1000, 0xffff), Err(Errno(14)));
        assert_eq!(p.alt_stack.flags, SS_DISABLE);
        assert_eq!(Kernel::sigaltstack(&mut p, 0x1000, 0), Ok(0));
        p.cpu.sp = 0x2400;
        assert_eq!(Kernel::sigaltstack(&mut p, 0x1000, 0), Err(Errno(1)));
        assert_eq!(Kernel::sigaltstack(&mut p, 0, 0x1100), Ok(0));
        assert_eq!(read_u32(&p.memory, 0x1108), Ok(1));
        assert_eq!(Kernel::membarrier(&p, 8, 0), Err(Errno(1)));
        assert_eq!(Kernel::membarrier(&p, 16, 0), Ok(0));
        assert_eq!(Kernel::membarrier(&p.fork(), 512, 0), Ok(16));
        assert_eq!(Kernel::membarrier(&p, 32, 0), Err(Errno(22)));
        let k = kernel();
        assert_eq!(k.ioprio_get(1, 1, 1), Ok(0));
        assert_eq!(k.ioprio_get(1, 1, 200), Err(Errno(3)));
        assert_eq!(k.ioprio_get(1, 3, 0), Err(Errno(95)));
    }
    #[test]
    fn file_size_limit_changes_are_atomic_and_hard_limit_is_privileged() {
        let mut p = process();
        write_u64(&mut p.memory, 0x1000, 10).unwrap();
        write_u64(&mut p.memory, 0x1008, 20).unwrap();
        assert_eq!(
            Kernel::rlimit(&mut p, 261, [0, 1, 0x1000, 0xffff, 0, 0]),
            Err(Errno(14))
        );
        assert_eq!(p.file_size_limit.get(), (u64::MAX, u64::MAX));
        assert_eq!(
            Kernel::rlimit(&mut p, 261, [0, 1, 0x1000, 0x1000, 0, 0]),
            Ok(0)
        );
        assert_eq!(p.file_size_limit.get(), (10, 20));
        assert_eq!(read_u64(&p.memory, 0x1000), Ok(u64::MAX));
        let child = p.fork();
        child.file_size_limit.set((5, 20));
        assert_eq!(p.file_size_limit.get(), (10, 20));
        p.linux.vfs.credentials.euid = 1000;
        write_u64(&mut p.memory, 0x1000, 10).unwrap();
        write_u64(&mut p.memory, 0x1008, 21).unwrap();
        assert_eq!(
            Kernel::rlimit(&mut p, 164, [1, 0x1000, 0, 0, 0, 0]),
            Err(Errno(1))
        );
        assert_eq!(p.file_size_limit.get(), (10, 20));
    }
}
