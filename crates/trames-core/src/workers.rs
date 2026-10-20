// SPDX-License-Identifier: GPL-3.0-or-later
//! CPU workers retain state and run until an event or a requested checkpoint.
//! Pure NativeCall and unfiltered snapshot/clock syscalls run on workers.
//! Mutable Linux/VFS state stays on the coordinator.
use std::{
    sync::{
        atomic::{AtomicBool, AtomicU64, AtomicUsize, Ordering},
        mpsc, Arc,
    },
    thread,
    time::{Duration, Instant},
};
use trames_cpu::{Cpu, DecodeStats, SliceOutcome};
use trames_memory::parallel::View;

/// Local interpreter polls, not scheduler quanta. Completing one of these does
/// not send a message, wait for peers, or give up the CPU or memory view.
const POLL_INSTRUCTIONS: u64 = 16_384;

pub(super) struct Control {
    stop: AtomicBool,
    remaining: AtomicU64,
}
impl Control {
    pub fn new(budget: u64) -> Self {
        Self {
            stop: AtomicBool::new(false),
            remaining: AtomicU64::new(budget),
        }
    }
    pub fn stop(&self) {
        self.stop.store(true, Ordering::Release);
    }
    fn claim(&self) -> Option<u64> {
        if self.stop.load(Ordering::Acquire) {
            return None;
        }
        match self
            .remaining
            .try_update(Ordering::Relaxed, Ordering::Relaxed, |left| {
                (left != 0).then(|| left.saturating_sub(POLL_INSTRUCTIONS))
            }) {
            Ok(left) => Some(left.min(POLL_INSTRUCTIONS)),
            Err(_) => {
                self.stop();
                None
            }
        }
    }
}

pub(super) struct Work {
    pub pid: u32,
    pub cpu: Cpu,
    pub memory: View,
    pub control: Arc<Control>,
    pub syscalls: Option<crate::worker_syscalls::Snapshot>,
}
pub(super) struct Done {
    pub pid: u32,
    pub cpu: Cpu,
    pub outcome: Option<SliceOutcome>,
    pub elapsed: Duration,
    pub before: DecodeStats,
    pub after: DecodeStats,
    pub worker: usize,
    /// All retired instructions, including earlier uninterrupted local polls.
    pub instructions: u64,
    pub chunks: u64,
    pub syscalls: u64,
    pub serviced_syscalls: u64,
    pub service_wait: Duration,
    pub syscall_time: Duration,
    pub native: trames_accel::native::Metrics,
    pub rejected_native: Option<(u64, u64, u64)>,
}
pub(super) enum Event {
    Done(Box<Done>),
    Service(Box<Request>),
    DiskRead(usize, Result<Vec<u8>, trames_vfs::Errno>),
}
/// Ownership of the stopped caller's parallel view travels with the request.
/// No coordinator Memory/Rc or page guard crosses this boundary.
pub(super) struct Request {
    pub pid: u32,
    pub number: u32,
    pub args: [u64; 6],
    pub memory: View,
    reply: mpsc::Sender<Reply>,
}
struct Reply {
    memory: View,
    value: Option<u64>,
    reply: mpsc::Sender<Reply>,
}
impl Request {
    /// None checkpoints before effects. A completed operation must always
    /// return its result, including when a concurrent stop has been requested.
    pub fn respond(self, value: Option<u64>) {
        let _ = self.reply.send(Reply {
            memory: self.memory,
            value,
            reply: self.reply.clone(),
        });
    }
}
/// Native scopes begin after stub validation; even unwinding releases the count.
struct NativeScope<'a>(Option<&'a AtomicUsize>);
impl Drop for NativeScope<'_> {
    fn drop(&mut self) {
        if let Some(active) = self.0 {
            active.fetch_sub(1, Ordering::Relaxed);
        }
    }
}
struct Worker {
    send: mpsc::Sender<Work>,
    join: thread::JoinHandle<()>,
    control: Option<Arc<Control>>,
}
struct DiskWorker {
    send: mpsc::Sender<(usize, trames_vfs::DiskReadJob)>,
    join: thread::JoinHandle<()>,
}
pub(super) struct Pool {
    workers: Vec<Option<Worker>>,
    send: mpsc::Sender<Event>,
    receive: mpsc::Receiver<Event>,
    active: Arc<AtomicUsize>,
    pub peak: Arc<AtomicUsize>,
    pub peak_native: Arc<AtomicUsize>,
    active_native: Arc<AtomicUsize>,
    profile_native_overlap: bool,
    disk_workers: Vec<DiskWorker>,
    next_disk_worker: usize,
}
impl Pool {
    #[cfg(test)]
    pub fn new(count: usize) -> Self {
        Self::with_profiling(count, true)
    }
    pub fn with_profiling(count: usize, profile_native_overlap: bool) -> Self {
        let (send, receive) = mpsc::channel();
        Self {
            workers: (0..count).map(|_| None).collect(),
            send,
            receive,
            active: Arc::new(AtomicUsize::new(0)),
            peak: Arc::new(AtomicUsize::new(0)),
            peak_native: Arc::new(AtomicUsize::new(0)),
            active_native: Arc::new(AtomicUsize::new(0)),
            profile_native_overlap,
            disk_workers: Vec::new(),
            next_disk_worker: 0,
        }
    }
    pub fn ensure(&mut self, index: usize) -> std::io::Result<()> {
        if self.workers[index].is_some() {
            return Ok(());
        }
        let (send, receive) = mpsc::channel::<Work>();
        let done = self.send.clone();
        let active = self.active.clone();
        let peak = self.peak.clone();
        let active_native = self.active_native.clone();
        let peak_native = self.peak_native.clone();
        let profile_native_overlap = self.profile_native_overlap;
        let join = thread::Builder::new()
            .name(format!("trames-cpu-{index}"))
            .spawn(move || {
                let mut registry = trames_accel::native::Registry::default();
                let (reply, response) = mpsc::channel();
                let mut reply = Some(reply);
                while let Ok(mut work) = receive.recv() {
                    let mut memory = Some(work.memory);
                    let before = work.cpu.decode_stats();
                    let began = Instant::now();
                    let mut instructions = 0;
                    let mut chunks = 0;
                    let mut running = false;
                    let mut rejected_native = None;
                    let mut credit = 0;
                    let mut syscalls = 0;
                    let mut syscall_time = Duration::ZERO;
                    let mut serviced_syscalls = 0;
                    let mut service_wait = Duration::ZERO;
                    let outcome = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                        loop {
                            if work.control.stop.load(Ordering::Acquire) {
                                break SliceOutcome::Yield { instructions: 0 };
                            }
                            if credit == 0 {
                                let Some(claimed) = work.control.claim() else {
                                    break SliceOutcome::Yield { instructions: 0 };
                                };
                                credit = claimed;
                            }
                            let budget = credit;
                            if !running {
                                if profile_native_overlap {
                                    let n = active.fetch_add(1, Ordering::Relaxed) + 1;
                                    peak.fetch_max(n, Ordering::Relaxed);
                                }
                                running = true;
                            }
                            let result = work
                                .cpu
                                .run_slice(memory.as_mut().expect("worker view"), budget);
                            chunks += 1;
                            let retired = match &result {
                                SliceOutcome::Fault(_) => work.cpu.fault_retired(),
                                SliceOutcome::Yield { instructions }
                                | SliceOutcome::Syscall { instructions }
                                | SliceOutcome::NativeCall { instructions, .. } => *instructions,
                            };
                            instructions += retired;
                            credit -= retired;
                            if matches!(result, SliceOutcome::Syscall { .. }) {
                                work.cpu.clear_exclusive();
                                if let Some(snapshot) = &work.syscalls {
                                    let began = snapshot.profile.then(Instant::now);
                                    if let Some(value) = snapshot
                                        .invoke(&work.cpu, memory.as_mut().expect("worker view"))
                                    {
                                        if let Some(began) = began {
                                            syscall_time += began.elapsed();
                                        }
                                        work.cpu.set_x(0, value);
                                        syscalls += 1;
                                        continue;
                                    }
                                    if crate::worker_syscalls::service_candidate(
                                        &work.cpu, snapshot,
                                    ) {
                                        let began = Instant::now();
                                        if profile_native_overlap {
                                            active.fetch_sub(1, Ordering::Relaxed);
                                        }
                                        running = false;
                                        let request = Request {
                                            pid: work.pid,
                                            number: work.cpu.x(8) as u32,
                                            args: std::array::from_fn(|i| work.cpu.x(i)),
                                            memory: memory.take().expect("worker view"),
                                            // The sole sender travels with the request
                                            // and returns in its reply. Dropping an
                                            // unanswered request must disconnect recv.
                                            reply: reply.take().expect("worker reply channel"),
                                        };
                                        assert!(
                                            done.send(Event::Service(Box::new(request))).is_ok(),
                                            "coordinator receiver"
                                        );
                                        // Never abandon an in-flight request on stop: it
                                        // may already have consumed input or written bytes.
                                        let answer = response.recv().expect("coordinator response");
                                        memory = Some(answer.memory);
                                        reply = Some(answer.reply);
                                        service_wait += began.elapsed();
                                        if let Some(value) = answer.value {
                                            work.cpu.set_x(0, value);
                                            serviced_syscalls += 1;
                                            continue;
                                        }
                                        break result;
                                    }
                                }
                            }
                            if let SliceOutcome::NativeCall {
                                pc,
                                module,
                                function,
                                ..
                            } = &result
                            {
                                work.cpu.clear_exclusive();
                                let args = [work.cpu.x(0), work.cpu.x(1), work.cpu.x(2)];
                                let native = registry.invoke_scoped(
                                    memory.as_mut().expect("worker view"),
                                    *pc,
                                    *module,
                                    *function,
                                    args,
                                    || {
                                        if profile_native_overlap {
                                            let n =
                                                active_native.fetch_add(1, Ordering::Relaxed) + 1;
                                            peak_native.fetch_max(n, Ordering::Relaxed);
                                            NativeScope(Some(&active_native))
                                        } else {
                                            NativeScope(None)
                                        }
                                    },
                                );
                                match native {
                                    Ok(value) => {
                                        work.cpu.set_x(0, value.value);
                                        work.cpu.set_x(1, u64::from(value.fallback));
                                        work.cpu.pc = pc + 4;
                                        instructions += 1;
                                        credit -= 1;
                                        continue;
                                    }
                                    Err(trames_accel::native::Error::Checkpoint) => {
                                        // The pending HLT is reserved for the
                                        // coordinator, and has no body effects.
                                        credit -= 1;
                                        break result;
                                    }
                                    Err(error) => {
                                        let fault = match error {
                                            trames_accel::native::Error::Memory(error) => {
                                                trames_cpu::CpuFault::Memory { pc: *pc, error }
                                            }
                                            trames_accel::native::Error::InvalidEntry => {
                                                rejected_native = Some((*pc, *module, *function));
                                                trames_cpu::CpuFault::Trap {
                                                    pc: *pc,
                                                    instruction: trames_accel::native::INSTRUCTION,
                                                    signal: 4,
                                                    code: 1,
                                                }
                                            }
                                            trames_accel::native::Error::Checkpoint => {
                                                unreachable!("handled checkpoint")
                                            }
                                        };
                                        break SliceOutcome::Fault(fault);
                                    }
                                }
                            }
                            if !matches!(result,
                                SliceOutcome::Yield { instructions } if instructions == budget
                            ) {
                                break result;
                            }
                        }
                    }))
                    .ok();
                    // Reuse credits across native returns. Refund only when this
                    // assignment ends, avoiding a global RMW pair per libc call.
                    work.control.remaining.fetch_add(credit, Ordering::Relaxed);
                    // Syscall, deferred native, fault and yield request a checkpoint
                    // immediately, before the coordinator receives this message.
                    work.control.stop();
                    let elapsed = began.elapsed().saturating_sub(service_wait);
                    let after = work.cpu.decode_stats();
                    if running && profile_native_overlap {
                        active.fetch_sub(1, Ordering::Relaxed);
                    }
                    // Completion publishes that this worker has released its
                    // immutable mapping view as well as stopped executing it.
                    // The joined coordinator can then update a uniquely owned
                    // snapshot without racing the message recipient.
                    drop(memory);
                    if done
                        .send(Event::Done(Box::new(Done {
                            pid: work.pid,
                            cpu: work.cpu,
                            outcome,
                            elapsed,
                            before,
                            after,
                            worker: index,
                            instructions,
                            chunks,
                            syscalls,
                            serviced_syscalls,
                            service_wait,
                            syscall_time,
                            native: {
                                registry.metrics.worker_calls = registry.metrics.calls;
                                std::mem::take(&mut registry.metrics)
                            },
                            rejected_native,
                        })))
                        .is_err()
                    {
                        break;
                    }
                }
            })?;
        self.workers[index] = Some(Worker {
            send,
            join,
            control: None,
        });
        Ok(())
    }
    pub fn dispatch(&mut self, index: usize, work: Work) -> Result<(), ()> {
        let worker = self.workers[index].as_mut().ok_or(())?;
        if worker.control.is_some() {
            return Err(());
        }
        worker.control = Some(work.control.clone());
        worker.send.send(work).map_err(|_| ())
    }
    pub fn disk_read(&mut self, id: usize, job: trames_vfs::DiskReadJob) {
        // Lazy persistent workers; at most one outstanding request per guest
        // CPU bounds queued jobs/buffers. Never create a thread per syscall.
        let index = self.next_disk_worker % self.workers.len().clamp(1, 2);
        self.next_disk_worker = self.next_disk_worker.wrapping_add(1);
        if index >= self.disk_workers.len() {
            let (send, receive) = mpsc::channel::<(usize, trames_vfs::DiskReadJob)>();
            let done = self.send.clone();
            if let Ok(join) = thread::Builder::new()
                .name("trames-disk".into())
                .spawn(move || {
                    while let Ok((id, job)) = receive.recv() {
                        let result =
                            std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| job.run()))
                                .unwrap_or(Err(trames_vfs::Errno(5)));
                        if done.send(Event::DiskRead(id, result)).is_err() {
                            break;
                        }
                    }
                })
            {
                self.disk_workers.push(DiskWorker { send, join });
            }
        }
        let job = if let Some(worker) = self.disk_workers.get(index) {
            match worker.send.send((id, job)) {
                Ok(()) => return,
                Err(error) => error.0 .1,
            }
        } else {
            job
        };
        // Thread resource failure preserves one execution and one completion.
        let _ = self.send.send(Event::DiskRead(id, job.run()));
    }
    fn received(&mut self, event: Event) -> Event {
        if let Event::Done(done) = &event {
            self.workers[done.worker]
                .as_mut()
                .expect("completed worker")
                .control = None;
        }
        event
    }
    pub fn event_timeout(&mut self, timeout: Duration) -> Result<Option<Event>, ()> {
        match self.receive.recv_timeout(timeout) {
            Ok(event) => Ok(Some(self.received(event))),
            Err(mpsc::RecvTimeoutError::Timeout) => Ok(None),
            Err(mpsc::RecvTimeoutError::Disconnected) => Err(()),
        }
    }
    #[cfg(test)]
    pub fn complete(&mut self) -> Result<Done, ()> {
        loop {
            let event = self.receive.recv().map_err(|_| ())?;
            match self.received(event) {
                Event::Done(done) => return Ok(*done),
                Event::Service(request) => request.respond(None),
                Event::DiskRead(_, _) => return Err(()),
            }
        }
    }
    #[cfg(test)]
    pub fn complete_timeout(&mut self, timeout: Duration) -> Result<Option<Done>, ()> {
        let deadline = Instant::now() + timeout;
        loop {
            match self.event_timeout(deadline.saturating_duration_since(Instant::now()))? {
                Some(Event::Done(done)) => return Ok(Some(*done)),
                Some(Event::Service(request)) => request.respond(None),
                Some(Event::DiskRead(_, _)) => return Err(()),
                None => return Ok(None),
            }
        }
    }
}

impl Drop for Pool {
    fn drop(&mut self) {
        for worker in self.workers.iter().flatten() {
            if let Some(control) = &worker.control {
                control.stop();
            }
        }
        // A stopped worker may be parked waiting for a queued service request.
        // Return its view before joining; no request is abandoned or replayed.
        while self.workers.iter().flatten().any(|w| w.control.is_some()) {
            match self.event_timeout(Duration::from_millis(10)) {
                Ok(Some(Event::Service(request))) => request.respond(None),
                Ok(Some(Event::Done(_))) => {}
                Ok(Some(Event::DiskRead(_, _))) => {}
                Ok(None) => {
                    for worker in self.workers.iter_mut().flatten() {
                        if worker.join.is_finished() {
                            worker.control = None;
                        }
                    }
                }
                Err(()) => break,
            }
        }
        for worker in self.workers.iter_mut().filter_map(Option::take) {
            drop(worker.send);
            let _ = worker.join.join();
        }
        for worker in self.disk_workers.drain(..) {
            drop(worker.send);
            let _ = worker.join.join();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::{
        cell::{Cell, RefCell},
        rc::Rc,
    };
    use trames_memory::{
        CpuMemory, Memory, MemoryError, NativeMemory, PageBacking, Permissions, PAGE_SIZE,
    };

    struct LivePage {
        bytes: RefCell<[u8; PAGE_SIZE as usize]>,
        version: Cell<u64>,
        unstable: Cell<bool>,
    }
    impl PageBacking for LivePage {
        fn parallel_key(&self) -> Option<(u64, u64)> {
            Some((self as *const Self as usize as u64, 0))
        }
        fn check(&self, _: u64) -> Result<(), MemoryError> {
            Ok(())
        }
        fn read(&self, offset: usize, out: &mut [u8]) -> Result<(), MemoryError> {
            out.copy_from_slice(&self.bytes.borrow()[offset..offset + out.len()]);
            if self.unstable.get() {
                self.version.set(self.version.get().wrapping_add(1));
            }
            Ok(())
        }
        fn write(&self, offset: usize, input: &[u8]) -> Result<(), MemoryError> {
            self.bytes.borrow_mut()[offset..offset + input.len()].copy_from_slice(input);
            self.version.set(self.version.get().wrapping_add(1));
            Ok(())
        }
        fn native_resident(&self) -> bool {
            true
        }
        fn sync(&self) -> Result<(), MemoryError> {
            Ok(())
        }
        fn version(&self) -> u64 {
            self.version.get()
        }
        fn writable(&self) -> bool {
            true
        }
        fn next_page(&self, _: u64) -> Result<Rc<dyn PageBacking>, MemoryError> {
            Err(MemoryError::InvalidRange)
        }
    }

    fn work(pid: u32, code: &[u32], count: u64, control: Arc<Control>) -> Work {
        let mut memory = Memory::default();
        memory.map(0x1000, PAGE_SIZE, Permissions::RW).unwrap();
        for (i, word) in code.iter().enumerate() {
            memory
                .write(0x1000 + i as u64 * 4, &word.to_le_bytes())
                .unwrap();
        }
        memory.protect(0x1000, PAGE_SIZE, Permissions::RX).unwrap();
        let mut cpu = Cpu::new(0x1000, 0);
        cpu.set_x(0, count);
        cpu.set_native_calls(true);
        Work {
            pid,
            cpu,
            memory: memory.parallel_view(&mut Default::default()).unwrap(),
            control,
            syscalls: None,
        }
    }

    fn syscall_snapshot() -> crate::worker_syscalls::Snapshot {
        crate::worker_syscalls::Snapshot {
            identities: [21, 12, 1000, 1001, 2000, 2001, 22],
            started: Instant::now(),
            processor: 1,
            profile: true,
            streams: [0; (trames_vfs::MAX_FDS as usize).div_ceil(64)],
            files: [0; (trames_vfs::MAX_FDS as usize).div_ceil(64)],
            fd_flags: Default::default(),
        }
    }

    fn query_work(control: Arc<Control>) -> Work {
        let mut job = work(1, &[0xd4000001, 0x14000000], 0, control);
        job.cpu.set_x(8, 62);
        job.cpu.set_x(1, 0);
        job.syscalls = Some(syscall_snapshot());
        job
    }
    #[test]
    fn reusable_reply_channel_handles_many_requests_and_views_without_replay() {
        let mut pool = Pool::new(1);
        pool.ensure(0).unwrap();
        let mut job = work(
            1,
            &[0xd4000001, 0x17ffffff],
            0,
            Arc::new(Control::new(1000)),
        );
        job.cpu.set_x(8, 62);
        job.syscalls = Some(syscall_snapshot());
        pool.dispatch(0, job).unwrap();
        for i in 0..128u64 {
            let Some(Event::Service(request)) = pool.event_timeout(Duration::from_secs(5)).unwrap()
            else {
                panic!("missing request")
            };
            if i != 0 {
                assert_eq!(request.args[0], i - 1);
            }
            assert_eq!(
                request.memory.read_array::<4>(0x1000).unwrap(),
                0xd4000001u32.to_le_bytes()
            );
            request.respond(Some(i));
        }
        let Some(Event::Service(request)) = pool.event_timeout(Duration::from_secs(5)).unwrap()
        else {
            panic!("missing final request")
        };
        assert_eq!(request.args[0], 127);
        request.respond(None);
        let done = pool.complete().unwrap();
        assert_eq!(done.serviced_syscalls, 128);
        assert_eq!(done.instructions, 257);
        assert!(matches!(done.outcome, Some(SliceOutcome::Syscall { .. })));
    }
    #[test]
    fn dropping_an_unanswered_request_disconnects_a_reusable_reply_receiver() {
        let mut pool = Pool::new(1);
        pool.ensure(0).unwrap();
        pool.dispatch(0, query_work(Arc::new(Control::new(100))))
            .unwrap();
        let Some(Event::Service(request)) = pool.event_timeout(Duration::from_secs(5)).unwrap()
        else {
            panic!("missing request")
        };
        drop(request);
        let done = pool
            .complete_timeout(Duration::from_secs(5))
            .unwrap()
            .expect("worker did not unblock");
        assert!(done.outcome.is_none());
    }

    #[test]
    fn native_peer_finishes_while_a_syscall_caller_is_parked_without_a_global_stop() {
        let mut pool = Pool::new(2);
        pool.ensure(0).unwrap();
        pool.ensure(1).unwrap();
        let control = Arc::new(Control::new(100_000));
        pool.dispatch(0, query_work(control.clone())).unwrap();
        let Some(Event::Service(request)) = pool.event_timeout(Duration::from_secs(5)).unwrap()
        else {
            panic!("missing query request");
        };
        assert!(!control.stop.load(Ordering::Acquire));
        let (mut peer, mut bytes) =
            native_work(3, [0x2000, 0x72, 128], Permissions::RW, control.clone());
        peer.pid = 2;
        pool.dispatch(1, peer).unwrap();
        let Some(Event::Done(peer)) = pool.event_timeout(Duration::from_secs(5)).unwrap() else {
            panic!("native peer could not progress while caller waited");
        };
        assert_eq!(peer.pid, 2);
        assert_eq!(peer.native.worker_calls, 1);
        assert!(matches!(peer.outcome, Some(SliceOutcome::Syscall { .. })));
        // Peer checkpoint arrived while this request was in flight. A completed
        // result must still be committed, never replayed on the coordinator.
        request.respond(Some(0x55));
        let caller = pool.complete().unwrap();
        assert_eq!(caller.cpu.x(0), 0x55);
        assert_eq!(caller.instructions, 1);
        assert_eq!(caller.serviced_syscalls, 1);
        assert!(matches!(caller.outcome, Some(SliceOutcome::Yield { .. })));
        bytes.commit_parallel().unwrap();
        assert_eq!(bytes.read_array::<128>(0x2000).unwrap(), [0x72; 128]);
        assert_eq!(control.remaining.load(Ordering::Relaxed), 100_000 - 7);
    }
    #[test]
    fn production_native_calls_do_not_update_global_overlap_instrumentation() {
        let mut pool = Pool::with_profiling(1, false);
        pool.ensure(0).unwrap();
        let (job, mut bytes) = native_work(
            3,
            [0x2000, 0x72, 128],
            Permissions::RW,
            Arc::new(Control::new(100)),
        );
        pool.dispatch(0, job).unwrap();
        let done = pool.complete().unwrap();
        assert_eq!(done.native.worker_calls, 1);
        assert_eq!(pool.peak_native.load(Ordering::Relaxed), 0);
        assert_eq!(pool.active_native.load(Ordering::Relaxed), 0);
        bytes.commit_parallel().unwrap();
        assert_eq!(bytes.read_array::<128>(0x2000).unwrap(), [0x72; 128]);
    }

    #[test]
    fn pool_drop_drains_a_queued_request_and_returns_its_view_before_join() {
        let (finished, completion) = mpsc::channel();
        thread::spawn(move || {
            let mut pool = Pool::new(1);
            pool.ensure(0).unwrap();
            let active = pool.active.clone();
            pool.dispatch(0, query_work(Arc::new(Control::new(100))))
                .unwrap();
            let Some(Event::Service(request)) = pool.event_timeout(Duration::from_secs(5)).unwrap()
            else {
                panic!("missing query request");
            };
            assert!(pool.send.send(Event::Service(request)).is_ok());
            drop(pool);
            assert_eq!(active.load(Ordering::Relaxed), 0);
            finished.send(()).unwrap();
        });
        completion
            .recv_timeout(Duration::from_secs(5))
            .expect("pool shutdown must drain requests");
    }

    #[test]
    fn local_syscalls_continue_with_exact_budget_and_fall_back_before_mutations() {
        let mut pool = Pool::new(1);
        pool.ensure(0).unwrap();
        for budget in 1..=5 {
            let control = Arc::new(Control::new(budget));
            // mov x8,#178; svc #0; mov x1,x0; mov x8,#64; svc #0.
            let mut work = work(
                22,
                &[0xd2801648, 0xd4000001, 0xaa0003e1, 0xd2800808, 0xd4000001],
                0,
                control.clone(),
            );
            work.syscalls = Some(syscall_snapshot());
            work.cpu.set_x(19, 0x1234);
            work.cpu.tpidr_el0 = 0x5678;
            pool.dispatch(0, work).unwrap();
            let done = pool
                .complete_timeout(Duration::from_secs(5))
                .unwrap()
                .unwrap();
            assert_eq!(done.instructions, budget);
            assert_eq!(control.remaining.load(Ordering::Relaxed), 0);
            assert_eq!(done.syscalls, u64::from(budget >= 2));
            assert_eq!(done.cpu.x(19), 0x1234);
            assert_eq!(done.cpu.tpidr_el0, 0x5678);
            if budget >= 2 {
                assert_eq!(done.cpu.x(0), 22);
            }
            if budget >= 3 {
                assert_eq!(done.cpu.x(1), 22);
            }
            if budget == 5 {
                assert!(matches!(done.outcome, Some(SliceOutcome::Syscall { .. })));
                assert_eq!(done.cpu.x(8), 64);
                assert_eq!(done.cpu.pc, 0x1014);
            }
        }
        // Filtered/trace assignments receive no snapshot and must checkpoint at
        // the original SVC, before the query returns or any following stores.
        let work = work(
            22,
            &[0xd2801648, 0xd4000001, 0xaa0003e1],
            0,
            Arc::new(Control::new(10)),
        );
        pool.dispatch(0, work).unwrap();
        let done = pool.complete().unwrap();
        assert_eq!(
            (done.instructions, done.syscalls, done.cpu.pc),
            (2, 0, 0x1008)
        );
        assert!(matches!(done.outcome, Some(SliceOutcome::Syscall { .. })));
    }

    #[test]
    fn repeated_queries_run_without_a_coordinator_rendezvous() {
        let mut pool = Pool::new(1);
        pool.ensure(0).unwrap();
        let budget = 65_539;
        let control = Arc::new(Control::new(budget));
        let mut work = work(22, &[0xd4000001, 0x17ffffff], 0, control.clone());
        work.cpu.set_x(8, 172);
        work.syscalls = Some(syscall_snapshot());
        pool.dispatch(0, work).unwrap();
        let done = pool
            .complete_timeout(Duration::from_secs(5))
            .unwrap()
            .unwrap();
        assert_eq!(done.instructions, budget);
        assert_eq!(done.syscalls, budget.div_ceil(2));
        assert_eq!(done.cpu.x(0), 21);
        assert_eq!(control.remaining.load(Ordering::Relaxed), 0);
        assert!(matches!(done.outcome, Some(SliceOutcome::Yield { .. })));
    }

    fn native_work(
        function: u64,
        args: [u64; 3],
        permissions: Permissions,
        control: Arc<Control>,
    ) -> (Work, Memory) {
        let mut memory = Memory::default();
        memory.map(0x1000, PAGE_SIZE, Permissions::RW).unwrap();
        memory.map(0x2000, PAGE_SIZE * 2, permissions).unwrap();
        let code = [
            0xd2800030u32,
            0xf2a00030,
            0xd2800011 | (function as u32) << 5,
            trames_accel::native::INSTRUCTION,
            0xd65f03c0,
            0xd4000001,
        ];
        for (i, word) in code.iter().enumerate() {
            memory
                .write(0x1000 + i as u64 * 4, &word.to_le_bytes())
                .unwrap();
        }
        memory.protect(0x1000, PAGE_SIZE, Permissions::RX).unwrap();
        let mut cpu = Cpu::new(0x1000, 0x3000);
        cpu.set_native_calls(true);
        for (i, a) in args.iter().enumerate() {
            cpu.set_x(i, *a);
        }
        cpu.set_x(30, 0x1014);
        cpu.set_x(19, 0x1234);
        cpu.tpidr_el0 = 0x5678;
        cpu.fpcr = 0x400000;
        cpu.fpsr = 17;
        let memory_view = memory.parallel_view(&mut Default::default()).unwrap();
        (
            Work {
                pid: 1,
                cpu,
                memory: memory_view,
                control,
                syscalls: None,
            },
            memory,
        )
    }

    #[test]
    fn deferred_native_reserves_hlt_before_effects_and_unstable_code_never_enters_body() {
        let mut pool = Pool::new(1);
        pool.ensure(0).unwrap();
        let control = Arc::new(Control::new(4));
        let (work, mut memory) = native_work(3, [0x2000, 7, 128], Permissions::RW, control.clone());
        let Work {
            cpu,
            memory: old_view,
            ..
        } = work;
        drop(old_view);
        let live = Rc::new(LivePage {
            bytes: RefCell::new([0; PAGE_SIZE as usize]),
            version: Cell::new(0),
            unstable: Cell::new(false),
        });
        memory
            .map_backed(0x2000, vec![live.clone()], false, Permissions::RW, true)
            .unwrap();
        let work = Work {
            pid: 1,
            cpu,
            memory: memory.parallel_view(&mut Default::default()).unwrap(),
            control: control.clone(),
            syscalls: None,
        };
        pool.dispatch(0, work).unwrap();
        let done = pool.complete().unwrap();
        assert!(matches!(
            done.outcome,
            Some(SliceOutcome::NativeCall { pc: 0x100c, .. })
        ));
        assert_eq!(done.instructions, 3);
        assert_eq!(control.remaining.load(Ordering::Relaxed), 0);
        assert_eq!(done.cpu.pc, 0x100c);
        assert_eq!((done.cpu.x(0), done.cpu.x(1)), (0x2000, 7));
        assert_eq!(done.native.checkpoint_retries, 1);
        assert_eq!(done.native.faults, 0);
        memory.commit_parallel().unwrap();
        assert_eq!(memory.read_array::<128>(0x2000).unwrap(), [0; 128]);
        let mut registry = trames_accel::native::Registry::default();
        let value = registry
            .invoke(
                &mut memory,
                0x100c,
                trames_accel::native::PROFILE,
                3,
                [0x2000, 7, 128],
            )
            .unwrap();
        assert!(!value.fallback);
        assert_eq!(memory.read_array::<128>(0x2000).unwrap(), [7; 128]);
        assert_eq!(live.bytes.borrow()[..128], [0; 128]);

        let code = Rc::new(LivePage {
            bytes: RefCell::new(memory.read_array(0x1000).unwrap()),
            version: Cell::new(0),
            unstable: Cell::new(true),
        });
        memory
            .map_backed(0x1000, vec![code.clone()], false, Permissions::RX, true)
            .unwrap();
        let entered = Cell::new(false);
        for _ in 0..2 {
            assert!(matches!(
                registry.invoke_scoped(
                    &mut memory,
                    0x100c,
                    trames_accel::native::PROFILE,
                    3,
                    [0x2000, 9, 128],
                    || entered.set(true)
                ),
                Err(trames_accel::native::Error::Checkpoint)
            ));
        }
        assert!(!entered.get());
        assert_eq!(registry.metrics.faults, 0);
        assert_eq!(registry.metrics.checkpoint_retries, 2);
        assert_eq!(memory.read_array::<128>(0x2000).unwrap(), [7; 128]);
        code.unstable.set(false);
        registry
            .invoke_scoped(
                &mut memory,
                0x100c,
                trames_accel::native::PROFILE,
                3,
                [0x2000, 9, 128],
                || entered.set(true),
            )
            .unwrap();
        assert!(entered.get());
        assert_eq!(memory.read_array::<128>(0x2000).unwrap(), [9; 128]);
    }

    #[test]
    fn a_peer_completes_native_work_while_another_native_body_is_blocked_on_its_page() {
        for (peer_address, held_length) in [(0x3000, 4096), (0x2100, 128)] {
            let mut pool = Pool::new(2);
            pool.ensure(0).unwrap();
            pool.ensure(1).unwrap();
            let control = Arc::new(Control::new(POLL_INSTRUCTIONS * 4));
            let (first, mut memory) =
                native_work(3, [0x2000, 0x6b, 128], Permissions::RW, control.clone());
            let mut held_view = first.memory.clone();
            let mut peer_cpu = first.cpu.clone();
            peer_cpu.set_x(0, peer_address);
            peer_cpu.set_x(1, 0x52);
            let peer = Work {
                pid: 2,
                cpu: peer_cpu,
                memory: first.memory.clone(),
                control: control.clone(),
                syscalls: None,
            };
            let (held_tx, held_rx) = mpsc::channel();
            let (release_tx, release_rx) = mpsc::channel();
            let held = thread::spawn(move || {
                held_view
                    .fill_with(0x2000, held_length, |bytes| {
                        held_tx.send(()).unwrap();
                        release_rx.recv_timeout(Duration::from_secs(5)).unwrap();
                        bytes.fill(0);
                    })
                    .unwrap()
            });
            held_rx.recv_timeout(Duration::from_secs(5)).unwrap();
            pool.dispatch(0, first).unwrap();
            let began = Instant::now();
            while pool.active_native.load(Ordering::Relaxed) == 0
                && began.elapsed() < Duration::from_secs(5)
            {
                thread::yield_now();
            }
            assert_eq!(pool.active_native.load(Ordering::Relaxed), 1);
            pool.dispatch(1, peer).unwrap();
            let peer = pool
                .complete_timeout(Duration::from_secs(5))
                .unwrap()
                .unwrap();
            assert_eq!(peer.pid, 2);
            assert_eq!(peer.instructions, 6);
            assert_eq!(peer.native.worker_calls, 1);
            assert!(matches!(peer.outcome, Some(SliceOutcome::Syscall { .. })));
            assert_eq!(pool.peak_native.load(Ordering::Relaxed), 2);
            release_tx.send(()).unwrap();
            held.join().unwrap();
            let first = pool
                .complete_timeout(Duration::from_secs(5))
                .unwrap()
                .unwrap();
            assert_eq!(first.pid, 1);
            assert_eq!(first.native.worker_calls, 1);
            assert_eq!(pool.active_native.load(Ordering::Relaxed), 0);
            memory.commit_parallel().unwrap();
            assert_eq!(memory.read_array::<128>(0x2000).unwrap(), [0x6b; 128]);
            assert_eq!(memory.read_array::<128>(peer_address).unwrap(), [0x52; 128]);
        }
    }

    #[test]
    fn native_returns_continue_locally_with_exact_retirement_and_guest_state() {
        let mut pool = Pool::new(1);
        pool.ensure(0).unwrap();
        for budget in 3..=6 {
            let control = Arc::new(Control::new(budget));
            let (work, mut memory) =
                native_work(3, [0x2000, 0x6b, 128], Permissions::RW, control.clone());
            pool.dispatch(0, work).unwrap();
            let done = pool
                .complete_timeout(Duration::from_secs(5))
                .unwrap()
                .unwrap();
            assert_eq!(done.instructions, budget);
            assert_eq!(control.remaining.load(Ordering::Relaxed), 0);
            assert_eq!(done.cpu.x(19), 0x1234);
            assert_eq!(done.cpu.sp, 0x3000);
            assert_eq!(done.cpu.tpidr_el0, 0x5678);
            assert_eq!(done.cpu.fpcr, 0x400000);
            assert_eq!(done.cpu.fpsr, 17);
            memory.commit_parallel().unwrap();
            if budget >= 4 {
                assert_eq!(done.native.worker_calls, 1);
                assert_eq!(done.native.calls, 1);
                assert_eq!(done.native.faults, 0);
                assert_eq!(memory.read_array::<128>(0x2000).unwrap(), [0x6b; 128]);
            } else {
                assert_eq!(done.native.calls, 0);
                assert_eq!(memory.read_array::<128>(0x2000).unwrap(), [0; 128]);
            }
            assert!(!matches!(
                done.outcome,
                Some(SliceOutcome::NativeCall { .. })
            ));
        }
        assert_eq!(pool.active_native.load(Ordering::Relaxed), 0);
    }

    #[test]
    fn native_fallback_retires_once_but_fault_refunds_its_credit() {
        let mut pool = Pool::new(1);
        pool.ensure(0).unwrap();
        let control = Arc::new(Control::new(4));
        let (work, mut memory) = native_work(
            3,
            [0x2000, 7, trames_accel::native::MAX_BULK_BYTES as u64 + 1],
            Permissions::RW,
            control.clone(),
        );
        pool.dispatch(0, work).unwrap();
        let done = pool.complete().unwrap();
        assert_eq!(done.instructions, 4);
        assert_eq!(done.cpu.pc, 0x1010);
        assert_eq!(done.cpu.x(0), 0x2000);
        assert_eq!(done.cpu.x(1), 1);
        assert_eq!(done.native.fallbacks, 1);
        assert_eq!(control.remaining.load(Ordering::Relaxed), 0);
        memory.commit_parallel().unwrap();
        assert_eq!(memory.read_array::<128>(0x2000).unwrap(), [0; 128]);
        let control = Arc::new(Control::new(4));
        let (work, mut memory) = native_work(3, [0x2000, 7, 128], Permissions::R, control.clone());
        pool.dispatch(0, work).unwrap();
        let done = pool.complete().unwrap();
        assert_eq!(done.instructions, 3);
        assert_eq!(done.cpu.pc, 0x100c);
        assert_eq!(done.cpu.x(0), 0x2000);
        assert_eq!(done.cpu.x(1), 7);
        assert_eq!(done.native.faults, 1);
        assert_eq!(control.remaining.load(Ordering::Relaxed), 1);
        assert!(matches!(
            done.outcome,
            Some(SliceOutcome::Fault(trames_cpu::CpuFault::Memory {
                pc: 0x100c,
                error: trames_memory::MemoryError::Permission {
                    address: 0x2000,
                    access: trames_memory::Access::Write
                }
            }))
        ));
        memory.commit_parallel().unwrap();
        assert_eq!(memory.read_array::<128>(0x2000).unwrap(), [0; 128]);
    }

    #[test]
    fn retains_cpu_through_many_polls_without_a_peer_or_coordinator_message() {
        let mut pool = Pool::new(2);
        pool.ensure(0).unwrap();
        let control = Arc::new(Control::new(1_000_000));
        // subs x0,x0,#1; b.ne -4; svc #0.
        pool.dispatch(
            0,
            work(1, &[0xf1000400, 0x54ffffe1, 0xd4000001], 100_000, control),
        )
        .unwrap();
        let done = pool
            .complete_timeout(Duration::from_secs(5))
            .unwrap()
            .unwrap();
        assert!(matches!(done.outcome, Some(SliceOutcome::Syscall { .. })));
        assert_eq!(done.instructions, 200_001);
        assert_eq!(done.cpu.x(0), 0);
        assert!(done.chunks > 10);
        assert_eq!(pool.active.load(Ordering::Relaxed), 0);
    }

    #[test]
    fn concurrent_workers_share_an_exact_non_divisible_budget() {
        let mut pool = Pool::new(2);
        for slot in 0..2 {
            pool.ensure(slot).unwrap();
        }
        for budget in [1, 17, POLL_INSTRUCTIONS + 3, 77_777] {
            let control = Arc::new(Control::new(budget));
            for slot in 0..2 {
                pool.dispatch(
                    slot,
                    work(slot as u32 + 1, &[0x14000000], 0, control.clone()),
                )
                .unwrap();
            }
            let a = pool.complete().unwrap();
            let b = pool.complete().unwrap();
            assert_eq!(a.instructions + b.instructions, budget);
            assert_eq!(control.remaining.load(Ordering::Relaxed), 0);
            assert_eq!(pool.active.load(Ordering::Relaxed), 0);
        }
    }

    #[test]
    fn rejected_native_does_not_retire_and_stops_queued_peers() {
        let mut pool = Pool::new(2);
        pool.ensure(0).unwrap();
        pool.ensure(1).unwrap();
        let control = Arc::new(Control::new(1));
        pool.dispatch(
            0,
            work(1, &[trames_accel::native::INSTRUCTION], 0, control.clone()),
        )
        .unwrap();
        let done = pool.complete().unwrap();
        assert!(matches!(
            done.outcome,
            Some(SliceOutcome::Fault(trames_cpu::CpuFault::Trap {
                signal: 4,
                ..
            }))
        ));
        assert_eq!(done.instructions, 0);
        assert_eq!(done.native.rejected, 1);
        assert_eq!(control.remaining.load(Ordering::Relaxed), 1);
        pool.dispatch(1, work(2, &[0x14000000], 0, control))
            .unwrap();
        let peer = pool.complete().unwrap();
        assert_eq!(peer.instructions, 0);
        assert_eq!(peer.chunks, 0);
        assert_eq!(pool.peak.load(Ordering::Relaxed), 1);
    }

    #[test]
    fn fault_reports_retirement_from_all_preceding_polls() {
        let mut pool = Pool::new(1);
        pool.ensure(0).unwrap();
        pool.dispatch(
            0,
            work(
                1,
                &[0xf1000400, 0x54ffffe1, 0],
                100_000,
                Arc::new(Control::new(1_000_000)),
            ),
        )
        .unwrap();
        let done = pool.complete().unwrap();
        assert!(matches!(done.outcome, Some(SliceOutcome::Fault(_))));
        assert_eq!(done.instructions, 200_000);
        assert!(done.cpu.fault_retired() < POLL_INSTRUCTIONS);
    }

    #[test]
    fn request_and_pool_teardown_stop_endless_guest_workers() {
        let mut pool = Pool::new(2);
        for slot in 0..2 {
            pool.ensure(slot).unwrap();
        }
        let control = Arc::new(Control::new(u64::MAX));
        for slot in 0..2 {
            pool.dispatch(
                slot,
                work(slot as u32 + 1, &[0x14000000], 0, control.clone()),
            )
            .unwrap();
        }
        assert!(pool
            .complete_timeout(Duration::from_millis(10))
            .unwrap()
            .is_none());
        control.stop();
        let a = pool.complete().unwrap();
        let b = pool.complete().unwrap();
        assert!(a.instructions + b.instructions > 0);
        assert_eq!(pool.active.load(Ordering::Relaxed), 0);
        let control = Arc::new(Control::new(u64::MAX));
        pool.dispatch(0, work(1, &[0x14000000], 0, control))
            .unwrap();
        // Drop requests a stop before joining, even without a caller-held control.
        drop(pool);
    }
}
