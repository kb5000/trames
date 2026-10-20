// SPDX-License-Identifier: GPL-3.0-or-later
//! In-epoch services: coordinator-owned FD state, caller-owned concurrent bytes.
//! Never inspect Process::memory/CPU or change mappings while peers execute.
use super::*;

impl Kernel {
    pub(super) fn service_request(
        &mut self,
        request: &mut crate::workers::Request,
        io: &mut impl GuestOutput,
    ) -> Option<u64> {
        let p = self.processes.get_mut(&request.pid)?;
        if p.seccomp.mode() != 0 || p.linux.trace || !matches!(p.state, State::Ready) {
            return None;
        }
        let a = request.args;
        let kind = p.linux.vfs.fd_kind(a[0] as u32);
        match request.number {
            25 if matches!(a[1], 1 | 3) => {}
            62 | 80 => {
                // Dynamic proc endpoints require a joined process snapshot.
                if matches!(&kind, Ok(FdKind::Node(path)) if path == b"/proc" || path.starts_with(b"/proc/") || path == b"/sys" || path.starts_with(b"/sys/"))
                {
                    return None;
                }
            }
            63..=70 | 82 | 83 => {
                // Start with streams that cannot modify mapped file backing,
                // transfer FD rights, acquire a terminal, or queue guest signals.
                // Ordinary files join only when retained inode/backing state
                // requires it. The cached bitmap is merely an RPC candidate.
                let write = matches!(request.number, 64 | 66 | 68 | 70);
                let stream = matches!(request.number, 63..=66)
                    && matches!(
                        &kind,
                        Ok(FdKind::PipeRead
                            | FdKind::PipeWrite
                            | FdKind::Stdin
                            | FdKind::Stdout
                            | FdKind::Stderr)
                            | Err(_)
                    );
                if !stream
                    && (!p.linux.vfs.epoch_file_io(a[0] as u32, write)
                    // RLIMIT_FSIZE can queue SIGXFSZ. Keep its complete existing
                    // signal path joined; no effects before this decision.
                    || write && p.file_size_limit.get().0 != u64::MAX)
                {
                    return None;
                }
            }
            _ => return None,
        }
        p.linux.vfs.set_process_context(request.pid);
        p.linux.vfs.set_lock_owner(p.tgid);
        p.linux.vfs.set_file_size_limit(p.file_size_limit.get().0);
        let began = self.profile_syscalls.then(Instant::now);
        let result = match request.number {
            63 => p.linux.read(a, &mut request.memory, io),
            64 => p.linux.write(a, &request.memory, io),
            65 => p.linux.readv(a, &mut request.memory, io),
            66 => p.linux.writev(a, &request.memory, io),
            67 | 68 => p
                .linux
                .positioned(a, &mut request.memory, request.number == 68),
            69 | 70 => p
                .linux
                .positioned_vector(a, &mut request.memory, request.number == 70),
            82 | 83 => p
                .linux
                .vfs
                .sync_fd(a[0] as u32, request.number == 83)
                .map(|()| 0),
            number => p.linux.fd_query(number, a, &mut request.memory),
        };
        if let Some(began) = began {
            self.syscall_total += began.elapsed();
        }
        match result {
            // Both return before effects on these admitted streams. Let the
            // original joined path install waits/timeouts and deliver SIGPIPE.
            Err(Errno(11))
                if p.linux
                    .vfs
                    .flags(a[0] as u32)
                    .is_ok_and(|f| f & O_NONBLOCK == 0) =>
            {
                None
            }
            Err(Errno(32)) if matches!(request.number, 64 | 66) => None,
            Err(Errno(38 | 95)) => None,
            other => Some(other.unwrap_or_else(error)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::workers::{Control, Event, Request, Work};
    use std::sync::Arc;
    use trames_memory::{CpuMemory, Permissions, PAGE_SIZE};

    fn pending(mut p: Process, number: u32, a: [u64; 6]) -> (Kernel, Box<Request>, Arc<Control>) {
        p.memory
            .write_array(0x4000, 0xd4000001u32.to_le_bytes())
            .unwrap();
        p.memory
            .protect(0x4000, PAGE_SIZE, Permissions::RX)
            .unwrap();
        let mut cpu = Cpu::new(0x4000, 0);
        cpu.set_x(8, number as u64);
        put_args(&mut cpu, a);
        let snapshot = crate::worker_syscalls::Snapshot {
            identities: [0; 7],
            started: Instant::now(),
            processor: 0,
            profile: false,
            streams: p.linux.vfs.stream_descriptors(),
            files: p.linux.vfs.file_descriptors(),
        };
        let memory = p.memory.parallel_view(&mut Default::default()).unwrap();
        let control = Arc::new(Control::new(100));
        let mut k = super::super::tests::kernel();
        k.processes.insert(1, p);
        k.workers.ensure(0).unwrap();
        k.workers
            .dispatch(
                0,
                Work {
                    pid: 1,
                    cpu,
                    memory,
                    control: control.clone(),
                    syscalls: Some(snapshot),
                },
            )
            .unwrap();
        let Some(Event::Service(request)) =
            k.workers.event_timeout(Duration::from_secs(5)).unwrap()
        else {
            panic!("missing in-epoch request");
        };
        (k, request, control)
    }
    fn finish(
        k: &mut Kernel,
        request: Box<Request>,
        control: Arc<Control>,
        value: Option<u64>,
    ) -> crate::workers::Done {
        control.stop();
        request.respond(value);
        let done = k.workers.complete().unwrap();
        assert_eq!(done.instructions, 1);
        assert_eq!(done.serviced_syscalls, u64::from(value.is_some()));
        k.processes
            .get_mut(&1)
            .unwrap()
            .memory
            .commit_parallel()
            .unwrap();
        done
    }
    #[test]
    fn regular_scalar_vector_and_positioned_services_share_the_original_abi() {
        for number in 63..=70u32 {
            let mut p = super::super::tests::process();
            p.linux
                .vfs
                .import_file(b"/file", b"abcdefgh".to_vec())
                .unwrap();
            let fd = p
                .linux
                .vfs
                .open_at(trames_vfs::AT_FDCWD, b"/file", 2, 0)
                .unwrap();
            let vector = matches!(number, 65 | 66 | 69 | 70);
            let positioned = number >= 67;
            let write = number % 2 == 0;
            p.memory.write(0x2200, b"XY").unwrap();
            p.memory.write(0x2210, b"ZW").unwrap();
            for (at, value) in [(0x2100, 0x2200), (0x2108, 2), (0x2110, 0x2210), (0x2118, 2)] {
                p.memory
                    .write_array(at, (value as u64).to_le_bytes())
                    .unwrap();
            }
            if positioned {
                p.linux.vfs.seek(fd, 5, 0).unwrap();
            }
            let args = [fd as u64, if vector { 0x2100 } else { 0x2200 }, 2, 2, 0, 0];
            let (mut k, mut request, control) = pending(p, number, args);
            let value =
                k.service_request(&mut request, &mut trames_linux::CapturedOutput::default());
            let count = if vector { 4 } else { 2 };
            assert_eq!(value, Some(count));
            let done = finish(&mut k, request, control, value);
            assert_eq!(done.cpu.x(0), count);
            let p = k.processes.get_mut(&1).unwrap();
            assert_eq!(
                p.linux.vfs.seek(fd, 0, 1).unwrap(),
                if positioned { 5 } else { count }
            );
            let start = if positioned { 2 } else { 0 };
            let mut expected = b"abcdefgh".to_vec();
            if write {
                expected[start..start + count as usize].copy_from_slice(&b"XYZW"[..count as usize]);
            } else {
                assert_eq!(
                    p.memory.read_array::<2>(0x2200).unwrap(),
                    expected[start..start + 2]
                );
                if vector {
                    assert_eq!(
                        p.memory.read_array::<2>(0x2210).unwrap(),
                        expected[start + 2..start + 4]
                    );
                }
            }
            assert_eq!(p.linux.vfs.pread(fd, 8, 0).unwrap(), expected);
        }
    }
    #[test]
    fn regular_file_sync_services_use_the_original_vfs_path() {
        for sync in [82, 83] {
            let mut p = super::super::tests::process();
            p.linux.vfs.import_file(b"/file", b"x".to_vec()).unwrap();
            let fd = p
                .linux
                .vfs
                .open_at(trames_vfs::AT_FDCWD, b"/file", 2, 0)
                .unwrap();
            let (mut k, mut request, control) = pending(p, sync, [fd as u64, 0, 0, 0, 0, 0]);
            let value =
                k.service_request(&mut request, &mut trames_linux::CapturedOutput::default());
            assert_eq!(value, Some(0));
            finish(&mut k, request, control, value);
        }
    }
    #[test]
    fn mapped_file_read_defers_until_shadow_stores_have_been_committed() {
        let mut p = super::super::tests::process();
        p.linux.vfs.import_file(b"/file", b"old".to_vec()).unwrap();
        let fd = p
            .linux
            .vfs
            .open_at(trames_vfs::AT_FDCWD, b"/file", 2, 0)
            .unwrap();
        let pages = p
            .linux
            .vfs
            .mapping_pages(fd, 0, PAGE_SIZE, true, true)
            .unwrap();
        p.memory
            .map_backed(0x6000, pages, true, Permissions::RW, true)
            .unwrap();
        let args = [fd as u64, 0x2000, 3, 0, 0, 0];
        let (mut k, mut request, control) = pending(p, 63, args);
        request.memory.write(0x6000, b"new").unwrap();
        assert_eq!(k.processes[&1].linux.vfs.pread(fd, 3, 0).unwrap(), b"old");
        let value = k.service_request(&mut request, &mut trames_linux::CapturedOutput::default());
        assert_eq!(value, None);
        let done = finish(&mut k, request, control, value);
        let mut p = k.processes.remove(&1).unwrap();
        p.cpu = done.cpu;
        k.call(
            1,
            &mut p,
            63,
            args,
            &mut trames_linux::CapturedOutput::default(),
        );
        assert_eq!(p.memory.read_array::<3>(0x2000).unwrap(), *b"new");
        assert_eq!(p.linux.vfs.seek(fd, 0, 1).unwrap(), 3);
    }
    #[test]
    fn finite_file_limit_keeps_sigxfsz_on_the_joined_path_without_effects() {
        let mut p = super::super::tests::process();
        p.linux.vfs.import_file(b"/file", b"old".to_vec()).unwrap();
        let fd = p
            .linux
            .vfs
            .open_at(trames_vfs::AT_FDCWD, b"/file", 2, 0)
            .unwrap();
        p.file_size_limit.set((0, 0));
        let args = [fd as u64, 0x2000, 1, 0, 0, 0];
        let (mut k, mut request, control) = pending(p, 64, args);
        let value = k.service_request(&mut request, &mut trames_linux::CapturedOutput::default());
        assert_eq!(value, None);
        let done = finish(&mut k, request, control, value);
        let mut p = k.processes.remove(&1).unwrap();
        p.cpu = done.cpu;
        k.call(
            1,
            &mut p,
            64,
            args,
            &mut trames_linux::CapturedOutput::default(),
        );
        assert_eq!(p.cpu.x(0), error(Errno(27)));
        assert!(p.pending.contains_key(&25));
        assert_eq!(p.linux.vfs.pread(fd, 3, 0).unwrap(), b"old");
    }
    #[test]
    fn vector_read_preflights_every_output_before_consuming_file_input() {
        let mut p = super::super::tests::process();
        p.linux.vfs.import_file(b"/file", b"abcd".to_vec()).unwrap();
        let fd = p
            .linux
            .vfs
            .open_at(trames_vfs::AT_FDCWD, b"/file", 0, 0)
            .unwrap();
        for (at, value) in [(0x2100, 0x2200), (0x2108, 2), (0x2110, 0x9000), (0x2118, 2)] {
            p.memory
                .write_array(at, (value as u64).to_le_bytes())
                .unwrap();
        }
        let (mut k, mut request, control) = pending(p, 65, [fd as u64, 0x2100, 2, 0, 0, 0]);
        let value = k.service_request(&mut request, &mut trames_linux::CapturedOutput::default());
        assert_eq!(value, Some(error(Errno(14))));
        finish(&mut k, request, control, value);
        assert_eq!(
            k.processes
                .get_mut(&1)
                .unwrap()
                .linux
                .vfs
                .seek(fd, 0, 1)
                .unwrap(),
            0
        );
    }
    #[test]
    fn pipe_read_uses_the_callers_view_and_commits_once_when_a_peer_requests_stop() {
        let mut p = super::super::tests::process();
        let [r, w] = p.linux.vfs.pipe(0).unwrap();
        p.linux.vfs.write(w, b"ab").unwrap();
        let (mut k, mut request, control) = pending(p, 63, [r as u64, 0x2000, 1, 0, 0, 0]);
        let value = k.service_request(&mut request, &mut trames_linux::CapturedOutput::default());
        assert_eq!(value, Some(1));
        assert_eq!(request.memory.read_array::<1>(0x2000).unwrap(), *b"a");
        assert_eq!(
            k.processes
                .get_mut(&1)
                .unwrap()
                .linux
                .vfs
                .read(r, 1)
                .unwrap(),
            b"b"
        );
        let done = finish(&mut k, request, control, value);
        assert_eq!(done.cpu.x(0), 1);
        assert!(matches!(done.outcome, Some(SliceOutcome::Yield { .. })));
        assert_eq!(
            k.processes[&1].memory.read_array::<1>(0x2000).unwrap(),
            *b"a"
        );
    }
    #[test]
    fn stream_preflight_rejects_bad_destinations_without_consuming_input() {
        let mut p = super::super::tests::process();
        let [r, w] = p.linux.vfs.pipe(0).unwrap();
        p.linux.vfs.write(w, b"ab").unwrap();
        let (mut k, mut request, control) = pending(p, 63, [r as u64, 0x9000, 2, 0, 0, 0]);
        let value = k.service_request(&mut request, &mut trames_linux::CapturedOutput::default());
        assert_eq!(value, Some(error(Errno(14))));
        assert_eq!(
            k.processes
                .get_mut(&1)
                .unwrap()
                .linux
                .vfs
                .read(r, 2)
                .unwrap(),
            b"ab"
        );
        finish(&mut k, request, control, value);
    }
    #[test]
    fn blocking_and_sigpipe_paths_checkpoint_but_nonblocking_eagain_completes() {
        for mode in 0..3 {
            let mut p = super::super::tests::process();
            let [r, w] = p
                .linux
                .vfs
                .pipe(if mode == 1 { O_NONBLOCK } else { 0 })
                .unwrap();
            let number = if mode == 2 { 64 } else { 63 };
            let fd = if mode == 2 {
                p.linux.vfs.close(r).unwrap();
                w
            } else {
                r
            };
            let a = [fd as u64, 0x2000, 1, 0, 0, 0];
            let (mut k, mut request, control) = pending(p, number, a);
            let mut io = trames_linux::CapturedOutput::default();
            let value = k.service_request(&mut request, &mut io);
            assert_eq!(value, (mode == 1).then(|| error(Errno(11))));
            let done = finish(&mut k, request, control, value);
            if mode != 1 {
                assert!(matches!(done.outcome, Some(SliceOutcome::Syscall { .. })));
                let mut p = k.processes.remove(&1).unwrap();
                p.cpu = done.cpu;
                k.call(1, &mut p, number, a, &mut io);
                if mode == 0 {
                    assert!(matches!(p.state, State::Blocked(63, _)));
                } else {
                    assert_eq!(p.cpu.x(0), error(Errno(32)));
                    assert!(p.pending.contains_key(&13));
                }
            }
        }
    }
}
