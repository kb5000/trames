// SPDX-License-Identifier: GPL-3.0-or-later
//! Syscalls whose authority is immutable during one parallel CPU epoch.
//! No VFS/Process Rc crosses the boundary. Mapping/process changes and filtered
//! calls still checkpoint; identities/mappings cannot change before join.
use std::time::{Instant, SystemTime, UNIX_EPOCH};
use trames_cpu::Cpu;
use trames_memory::{Access, CpuMemory};
use trames_vfs::Errno;

pub(super) struct Snapshot {
    /// getpid, getppid, getuid, geteuid, getgid, getegid, gettid.
    pub identities: [u64; 7],
    pub started: Instant,
    pub processor: u32,
    pub profile: bool,
    pub streams: trames_vfs::FdMask,
}

/// These calls can request the coordinator without stopping peer CPUs. The
/// service still admits the actual descriptor kind before doing any effects.
pub(super) fn service_candidate(cpu: &Cpu, snapshot: &Snapshot) -> bool {
    match cpu.x(8) as u32 {
        25 => matches!(cpu.x(1), 1 | 3),
        62 | 80 => true,
        63 | 64 => {
            let fd = cpu.x(0) as u32;
            fd < trames_vfs::MAX_FDS && snapshot.streams[fd as usize / 64] & (1 << (fd % 64)) != 0
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
    if !matches!(id, 0 | 1 | 4 | 6 | 7) {
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
        let elapsed = if matches!(id, 0 | 6) {
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

impl Snapshot {
    pub fn invoke(&self, cpu: &Cpu, memory: &mut impl CpuMemory) -> Option<u64> {
        let result = match cpu.x(8) as u32 {
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
    fn clock_and_processor_writes_use_checked_shared_guest_memory() {
        let mut memory = Memory::default();
        memory.map(0x1000, PAGE_SIZE * 2, Permissions::RW).unwrap();
        memory.write_array(0x1ff8, [0x5a; 8]).unwrap();
        memory.protect(0x2000, PAGE_SIZE, Permissions::R).unwrap();
        let snapshot = Snapshot {
            identities: [1, 0, 1000, 1001, 2000, 2001, 2],
            started: Instant::now(),
            processor: 3,
            profile: false,
            streams: [0; (trames_vfs::MAX_FDS as usize).div_ceil(64)],
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
