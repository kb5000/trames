// SPDX-License-Identifier: GPL-3.0-or-later
//! Syscalls whose authority is immutable during one parallel CPU epoch.
//! No VFS/Process Rc crosses the boundary. Only seccomp-allowed calls execute;
//! filter installation, tracing and mutable authority changes still checkpoint.
use std::{
    sync::Arc,
    time::{Duration, Instant, SystemTime, UNIX_EPOCH},
};
use trames_cpu::Cpu;
use trames_memory::{Access, CpuMemory};
use trames_vfs::Errno;

pub(super) struct Snapshot {
    pub seccomp: crate::process::seccomp::Seccomp,
    /// getpid, getppid, getuid, geteuid, getgid, getegid, gettid.
    pub identities: [u64; 7],
    pub started: Instant,
    pub processor: u32,
    pub profile: bool,
    pub streams: trames_vfs::FdMask,
    pub files: trames_vfs::FdMask,
    pub fd_flags: trames_vfs::FdFlags,
    pub direct: trames_linux::DirectSyscalls,
    pub futex: crate::futex::Private,
}

/// These calls can request the coordinator without stopping peer CPUs. The
/// service still admits the actual descriptor kind before doing any effects.
pub(super) fn service_candidate(cpu: &Cpu, snapshot: &Snapshot) -> bool {
    match cpu.x(8) as u32 {
        62 | 80 => true,
        63..=66 => {
            let fd = cpu.x(0) as u32;
            fd < trames_vfs::MAX_FDS
                && (snapshot.streams[fd as usize / 64] | snapshot.files[fd as usize / 64])
                    & (1 << (fd % 64))
                    != 0
        }
        67..=70 | 82 | 83 => {
            let fd = cpu.x(0) as u32;
            fd < trames_vfs::MAX_FDS && snapshot.files[fd as usize / 64] & (1 << (fd % 64)) != 0
        }
        _ => false,
    }
}

pub(super) fn clock(
    memory: &mut impl CpuMemory,
    number: u32,
    id: u64,
    address: u64,
    started: Instant,
) -> Result<u64, Errno> {
    if !matches!(id, 0 | 1 | 4..=7) {
        return Err(Errno(22));
    }
    if number == 114 && address == 0 {
        return Ok(0);
    }
    memory
        .check(address, 16, Access::Write)
        .map_err(|_| Errno(14))?;
    let (seconds, nanos) = if number == 114 {
        (0, 1)
    } else {
        // Linux 5 is CLOCK_REALTIME_COARSE; 6 is CLOCK_MONOTONIC_COARSE.
        // Both coarse clocks use the same epoch as their high-resolution
        // counterpart. Mixing Unix time with session-monotonic deadlines can
        // turn GLib/Gecko event waits into an effectively endless busy loop.
        let elapsed = if matches!(id, 0 | 5) {
            SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map_err(|_| Errno(5))?
        } else {
            started.elapsed()
        };
        (elapsed.as_secs(), elapsed.subsec_nanos() as u64)
    };
    let mut out = [0; 16];
    out[..8].copy_from_slice(&seconds.to_le_bytes());
    out[8..].copy_from_slice(&nanos.to_le_bytes());
    memory.write_array(address, out).map_err(|_| Errno(14))?;
    Ok(0)
}

pub(super) fn futex_wait_parameters(
    memory: &impl CpuMemory,
    a: [u64; 6],
    started: Instant,
) -> Result<(u32, Option<Instant>), Errno> {
    let bitset = a[1] & 127 == 9;
    let bits = if bitset { a[5] as u32 } else { u32::MAX };
    if bits == 0 {
        return Err(Errno(22));
    }
    let deadline = if a[3] == 0 {
        None
    } else {
        let bytes = memory.read_array::<16>(a[3]).map_err(|_| Errno(14))?;
        let sec = u64::from_le_bytes(bytes[..8].try_into().unwrap());
        let ns = u64::from_le_bytes(bytes[8..].try_into().unwrap());
        if sec > i64::MAX as u64 || ns >= 1_000_000_000 {
            return Err(Errno(22));
        }
        let mut duration = Duration::new(sec, ns as u32);
        if bitset {
            let now = if a[1] & 256 != 0 {
                SystemTime::now()
                    .duration_since(UNIX_EPOCH)
                    .map_err(|_| Errno(5))?
            } else {
                started.elapsed()
            };
            duration = duration.saturating_sub(now);
        } else if a[1] & 256 != 0 {
            return Err(Errno(38));
        }
        Some(Instant::now().checked_add(duration).ok_or(Errno(22))?)
    };
    Ok((bits, deadline))
}

impl Snapshot {
    fn private_futex_wait(&self, cpu: &Cpu) -> bool {
        cpu.x(8) as u32 == 98 && matches!(cpu.x(1), 128 | 384 | 137 | 393)
    }
    pub fn wait(
        &self,
        cpu: &Cpu,
        memory: &mut impl CpuMemory,
        pid: u32,
        epoch: &Arc<crate::run_queue::Epoch>,
    ) -> Option<Result<crate::run_queue::WaitState, Errno>> {
        if !self.private_futex_wait(cpu) {
            return None;
        }
        Some((|| {
            if cpu.x(0) & 3 != 0 {
                return Err(Errno(22));
            }
            memory
                .check(cpu.x(0), 4, Access::Read)
                .map_err(|_| Errno(14))?;
            let (bits, deadline) =
                futex_wait_parameters(memory, std::array::from_fn(|i| cpu.x(i)), self.started)?;
            let key = (0, self.futex.vm, cpu.x(0) & 0x00ff_ffff_ffff_ffff);
            let wait = self.futex.table.wait_target(
                key,
                pid,
                bits,
                Some(Arc::downgrade(epoch)),
                || {
                    let value = u32::from_le_bytes(
                        memory.read_array::<4>(cpu.x(0)).map_err(|_| Errno(14))?,
                    );
                    if value != cpu.x(2) as u32 {
                        return Err(Errno(11));
                    }
                    Ok(())
                },
            )?;
            Ok(crate::run_queue::WaitState { wait, deadline })
        })())
    }
    fn private_futex_wake(&self, cpu: &Cpu) -> bool {
        cpu.x(8) as u32 == 98 && matches!(cpu.x(1), 129 | 138)
    }
    pub fn admit(&self, cpu: &Cpu) -> bool {
        if self.seccomp.mode() == 0 {
            return true;
        }
        // Unsupported calls go straight to the joined kernel, which evaluates
        // their filter once. In particular, do not run BPF twice for each
        // futex/epoll checkpoint. Ordinary unfiltered calls keep the old path.
        if !matches!(cpu.x(8) as u32, 25 | 113 | 114 | 124 | 168 | 172..=178)
            && !self
                .direct
                .supports_call(cpu.x(8) as u32, std::array::from_fn(|i| cpu.x(i)))
            && !service_candidate(cpu, self)
            && !self.private_futex_wake(cpu)
            && !self.private_futex_wait(cpu)
        {
            return false;
        }
        self.seccomp
            .allows(cpu.x(8) as u32, cpu.pc, std::array::from_fn(|i| cpu.x(i)))
    }
    pub fn invoke(&self, cpu: &Cpu, memory: &mut impl CpuMemory) -> Option<u64> {
        if let Some(result) =
            self.direct
                .invoke_file(cpu.x(8) as u32, std::array::from_fn(|i| cpu.x(i)), memory)
        {
            return Some(result.unwrap_or_else(|e| 0u64.wrapping_sub(e.0 as u64)));
        }
        let result = match cpu.x(8) as u32 {
            number @ (29 | 63..=66 | 73 | 86 | 87 | 206 | 207 | 210..=212) => {
                self.direct
                    .invoke(number, std::array::from_fn(|i| cpu.x(i)), memory)?
            }
            98 if self.private_futex_wake(cpu) => (|| {
                // Match the joined futex ABI checks before touching any queue.
                if cpu.x(0) & 3 != 0 {
                    return Err(Errno(22));
                }
                memory
                    .check(cpu.x(0), 4, Access::Read)
                    .map_err(|_| Errno(14))?;
                if cpu.x(2) > i32::MAX as u64 || cpu.x(1) == 138 && cpu.x(5) as u32 == 0 {
                    return Err(Errno(22));
                }
                let bits = if cpu.x(1) == 138 {
                    cpu.x(5) as u32
                } else {
                    u32::MAX
                };
                Ok(self.futex.wake(cpu.x(0), cpu.x(2) as u32, bits))
            })(),
            25 if matches!(cpu.x(1), 1 | 3) => self
                .fd_flags
                .get(cpu.x(0) as u32)
                .and_then(|flags| trames_vfs::query_fd_flags(flags, cpu.x(1))),
            number @ 172..=178 => Ok(self.identities[(number - 172) as usize]),
            number @ (113 | 114) => clock(memory, number, cpu.x(0), cpu.x(1), self.started),
            168 => (|| {
                let cpu_address = cpu.x(0);
                let node_address = cpu.x(1);
                // As on the coordinator, validate both destinations before
                // either store. Writes use the interpreter's exact view/model.
                for address in [cpu_address, node_address] {
                    if address != 0 {
                        memory
                            .check(address, 4, Access::Write)
                            .map_err(|_| Errno(14))?;
                    }
                }
                if cpu_address != 0 {
                    memory
                        .write_array(cpu_address, self.processor.to_le_bytes())
                        .map_err(|_| Errno(14))?;
                }
                if node_address != 0 {
                    memory
                        .write_array(node_address, 0u32.to_le_bytes())
                        .map_err(|_| Errno(14))?;
                }
                Ok(0)
            })(),
            _ => return None,
        };
        Some(result.unwrap_or_else(|error| 0u64.wrapping_sub(error.0 as u64)))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use trames_memory::{Memory, Permissions, PAGE_SIZE};
    #[test]
    fn shared_wait_parser_preserves_absolute_clocks_flags_and_error_order() {
        let mut memory = Memory::default();
        memory.map(0x1000, PAGE_SIZE, Permissions::RW).unwrap();
        let started = Instant::now();
        for op in [137, 393] {
            let (bits, deadline) =
                futex_wait_parameters(&memory, [0x1000, op, 0, 0x1100, 0, 2], started).unwrap();
            assert_eq!(bits, 2);
            assert!(deadline.unwrap() <= Instant::now());
        }
        assert_eq!(
            futex_wait_parameters(&memory, [0, 384, 0, 0x1100, 0, 0], started),
            Err(Errno(38))
        );
        assert_eq!(
            futex_wait_parameters(&memory, [0, 384, 0, 0, 0, 0], started),
            Ok((u32::MAX, None))
        );
        memory
            .write_array(0x1108, 1_000_000_000u64.to_le_bytes())
            .unwrap();
        assert_eq!(
            futex_wait_parameters(&memory, [0, 128, 0, 0x1100, 0, 0], started),
            Err(Errno(22))
        );
        assert_eq!(
            futex_wait_parameters(&memory, [0, 128, 0, 0x1ff8, 0, 0], started),
            Err(Errno(14))
        );
        assert_eq!(
            futex_wait_parameters(&memory, [0, 137, 0, 0x3000, 0, 0], started),
            Err(Errno(22))
        );
    }
    #[test]
    fn private_wakes_claim_waiters_and_preserve_bitsets_and_validation() {
        let mut memory = Memory::default();
        memory.map(0x1000, PAGE_SIZE, Permissions::RW).unwrap();
        memory.write_array(0x1000, 17u32.to_le_bytes()).unwrap();
        let table = crate::futex::Futexes::default();
        let first = table
            .wait((0, 7, 0x1000), 1, 2, || Ok::<_, ()>(()))
            .unwrap();
        let second = table
            .wait((0, 7, 0x1000), 2, 2, || Ok::<_, ()>(()))
            .unwrap();
        let other_vm = table
            .wait((0, 8, 0x1000), 3, 2, || Ok::<_, ()>(()))
            .unwrap();
        let snapshot = Snapshot {
            seccomp: Default::default(),
            identities: [0; 7],
            started: Instant::now(),
            processor: 0,
            profile: false,
            streams: Default::default(),
            files: Default::default(),
            fd_flags: Default::default(),
            direct: Default::default(),
            futex: crate::futex::Private::new(table, 7),
        };
        let mut view = memory.parallel_view(&mut Default::default()).unwrap();
        let mut cpu = Cpu::new(0x4004, 0);
        cpu.set_x(8, 98);
        cpu.set_x(0, 0x1004);
        cpu.set_x(1, 129);
        cpu.set_x(2, 1);
        assert_eq!(snapshot.invoke(&cpu, &mut view), Some(0));
        cpu.set_x(0, 0x1000);
        assert_eq!(snapshot.invoke(&cpu, &mut view), Some(1));
        assert!(first.woken());
        assert!(!second.woken() && !other_vm.woken());
        assert!(snapshot.futex.take_readied());
        cpu.set_x(1, 138);
        cpu.set_x(5, 4);
        assert_eq!(snapshot.invoke(&cpu, &mut view), Some(0));
        cpu.set_x(5, 2);
        assert_eq!(snapshot.invoke(&cpu, &mut view), Some(1));
        assert!(second.woken() && !other_vm.woken());
        assert_eq!(snapshot.invoke(&cpu, &mut view), Some(0));
        assert!(snapshot.futex.take_readied());
        cpu.set_x(2, 0);
        assert_eq!(snapshot.invoke(&cpu, &mut view), Some(0));
        cpu.set_x(5, 0);
        assert_eq!(
            snapshot.invoke(&cpu, &mut view),
            Some(0u64.wrapping_sub(22))
        );
        cpu.set_x(1, 129);
        cpu.set_x(0, 0x1005);
        assert_eq!(
            snapshot.invoke(&cpu, &mut view),
            Some(0u64.wrapping_sub(22))
        );
        cpu.set_x(0, 0x2000);
        assert_eq!(
            snapshot.invoke(&cpu, &mut view),
            Some(0u64.wrapping_sub(14))
        );
        cpu.set_x(0, 0x1004);
        cpu.set_x(2, 1 << 31);
        assert_eq!(
            snapshot.invoke(&cpu, &mut view),
            Some(0u64.wrapping_sub(22))
        );
        cpu.set_x(1, 1); // Shared-key wake must use the actual backing key.
        assert_eq!(snapshot.invoke(&cpu, &mut view), None);
        assert_eq!(view.read_array::<4>(0x1000).unwrap(), 17u32.to_le_bytes());
    }
    #[test]
    fn coarse_clocks_share_the_epoch_of_their_high_resolution_counterparts() {
        let mut memory = Memory::default();
        memory.map(0x1000, PAGE_SIZE, Permissions::RW).unwrap();
        let snapshot = Snapshot {
            seccomp: Default::default(),
            identities: [0; 7],
            started: Instant::now(),
            processor: 0,
            profile: false,
            streams: [0; (trames_vfs::MAX_FDS as usize).div_ceil(64)],
            files: [0; (trames_vfs::MAX_FDS as usize).div_ceil(64)],
            fd_flags: Default::default(),
            direct: Default::default(),
            futex: Default::default(),
        };
        let mut view = memory.parallel_view(&mut Default::default()).unwrap();
        let mut cpu = Cpu::new(0, 0);
        cpu.set_x(8, 113);
        cpu.set_x(1, 0x1000);
        for id in [0, 5, 1, 4, 6, 7] {
            let before = if matches!(id, 0 | 5) {
                SystemTime::now().duration_since(UNIX_EPOCH).unwrap()
            } else {
                snapshot.started.elapsed()
            };
            cpu.set_x(0, id);
            assert_eq!(snapshot.invoke(&cpu, &mut view), Some(0));
            let seconds = view.read_u64(0x1000).unwrap();
            let nanos = view.read_u64(0x1008).unwrap();
            assert!(nanos < 1_000_000_000);
            let stamp = std::time::Duration::new(seconds, nanos as u32);
            let after = if matches!(id, 0 | 5) {
                SystemTime::now().duration_since(UNIX_EPOCH).unwrap()
            } else {
                snapshot.started.elapsed()
            };
            assert!(before <= stamp && stamp <= after, "clock {id}: {stamp:?}");
            cpu.set_x(8, 114);
            cpu.set_x(1, 0);
            assert_eq!(snapshot.invoke(&cpu, &mut view), Some(0));
            cpu.set_x(8, 113);
            cpu.set_x(1, 0x1000);
        }
    }
    #[test]
    fn fd_queries_complete_locally_but_mutating_fcntl_keeps_checkpointing() {
        let mut vfs = trames_vfs::Vfs::default();
        let [fd, _] = vfs
            .pipe(trames_vfs::O_CLOEXEC | trames_vfs::O_NONBLOCK)
            .unwrap();
        let snapshot = Snapshot {
            seccomp: Default::default(),
            identities: [0; 7],
            started: Instant::now(),
            processor: 0,
            profile: false,
            streams: vfs.stream_descriptors(),
            files: vfs.file_descriptors(),
            fd_flags: vfs.fd_flags_snapshot(),
            direct: Default::default(),
            futex: Default::default(),
        };
        let mut memory = Memory::default();
        let mut cpu = Cpu::new(0, 0);
        cpu.set_x(8, 25);
        cpu.set_x(0, fd as u64);
        cpu.set_x(1, 1);
        assert_eq!(snapshot.invoke(&cpu, &mut memory), Some(1));
        cpu.set_x(1, 3);
        assert_eq!(
            snapshot.invoke(&cpu, &mut memory),
            Some(trames_vfs::O_NONBLOCK as u64)
        );
        cpu.set_x(1, 4);
        assert_eq!(snapshot.invoke(&cpu, &mut memory), None);
        assert!(!service_candidate(&cpu, &snapshot));
        cpu.set_x(0, u32::MAX as u64);
        cpu.set_x(1, 3);
        assert_eq!(
            snapshot.invoke(&cpu, &mut memory),
            Some(0u64.wrapping_sub(9))
        );
    }

    #[test]
    fn clock_and_processor_writes_use_checked_shared_guest_memory() {
        let mut memory = Memory::default();
        memory.map(0x1000, PAGE_SIZE * 2, Permissions::RW).unwrap();
        memory.write_array(0x1ff8, [0x5a; 8]).unwrap();
        memory.protect(0x2000, PAGE_SIZE, Permissions::R).unwrap();
        let snapshot = Snapshot {
            seccomp: Default::default(),
            identities: [1, 0, 1000, 1001, 2000, 2001, 2],
            started: Instant::now(),
            processor: 3,
            profile: false,
            streams: [0; (trames_vfs::MAX_FDS as usize).div_ceil(64)],
            files: [0; (trames_vfs::MAX_FDS as usize).div_ceil(64)],
            fd_flags: Default::default(),
            direct: Default::default(),
            futex: Default::default(),
        };
        let mut view = memory.parallel_view(&mut Default::default()).unwrap();
        let mut cpu = Cpu::new(0, 0);
        cpu.set_x(8, 113);
        cpu.set_x(0, 1);
        cpu.set_x(1, 0x1ff8);
        assert_eq!(
            snapshot.invoke(&cpu, &mut view),
            Some(0u64.wrapping_sub(14))
        );
        assert_eq!(view.read_array::<8>(0x1ff8).unwrap(), [0x5a; 8]);
        cpu.set_x(0, 2);
        cpu.set_x(1, 0);
        assert_eq!(
            snapshot.invoke(&cpu, &mut view),
            Some(0u64.wrapping_sub(22))
        );
        cpu.set_x(8, 114);
        cpu.set_x(0, 1);
        assert_eq!(snapshot.invoke(&cpu, &mut view), Some(0));
        cpu.set_x(1, 0xab00000000001800);
        assert_eq!(snapshot.invoke(&cpu, &mut view), Some(0));
        assert_eq!(
            view.read_array::<16>(0x1800).unwrap(),
            [0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0]
        );
        cpu.set_x(8, 113);
        assert_eq!(snapshot.invoke(&cpu, &mut view), Some(0));
        assert!(view.read_u64(0x1808).unwrap() < 1_000_000_000);
        cpu.set_x(8, 168);
        cpu.set_x(0, 0x1ff8);
        cpu.set_x(1, 0x2000);
        assert_eq!(
            snapshot.invoke(&cpu, &mut view),
            Some(0u64.wrapping_sub(14))
        );
        assert_eq!(view.read_array::<8>(0x1ff8).unwrap(), [0x5a; 8]);
        cpu.set_x(1, 0x1ffc);
        assert_eq!(snapshot.invoke(&cpu, &mut view), Some(0));
        drop(view);
        memory.commit_parallel().unwrap();
        assert_eq!(
            memory.read_array::<8>(0x1ff8).unwrap(),
            [3, 0, 0, 0, 0, 0, 0, 0]
        );
    }
}
