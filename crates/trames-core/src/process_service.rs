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
            63 | 64 => {
                // Start with streams that cannot modify mapped file backing,
                // transfer FD rights, acquire a terminal, or queue guest signals.
                // Regular/mapped files, sockets, devices and PTYs join as before.
                if !matches!(
                    kind,
                    Ok(FdKind::PipeRead
                        | FdKind::PipeWrite
                        | FdKind::Stdin
                        | FdKind::Stdout
                        | FdKind::Stderr)
                        | Err(_)
                ) {
                    return None;
                }
            }
            _ => return None,
        }
        p.linux.vfs.set_process_context(request.pid);
        p.linux.vfs.set_lock_owner(p.tgid);
        let began = self.profile_syscalls.then(Instant::now);
        let result = match request.number {
            63 => p.linux.read(a, &mut request.memory, io),
            64 => p.linux.write(a, &request.memory, io),
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
            Err(Errno(32)) if request.number == 64 => None,
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
