// SPDX-License-Identifier: GPL-3.0-or-later
//! A frozen VM cohort owns every execution View until its workers have stopped.
//! Ready/parked tasks move between existing workers; no coordinator RPC or spin.
use crate::{
    futex::{Ticket, Wait},
    workers::{Control, Done, Work},
};
use std::{
    collections::{BTreeMap, BTreeSet, VecDeque},
    sync::{
        atomic::{AtomicU64, Ordering},
        Arc, Condvar, Mutex, Weak,
    },
    time::{Duration, Instant},
};
use trames_cpu::{DecodeStats, SliceOutcome};

#[derive(Clone)]
pub(super) struct WaitState {
    pub wait: Arc<Wait>,
    pub deadline: Option<Instant>,
}
#[derive(Clone, Copy)]
pub(super) struct IoCall {
    pub number: u32,
    pub args: [u64; 6],
    pub deadline: Option<Instant>,
}
impl From<&trames_linux::PendingIo> for IoCall {
    fn from(io: &trames_linux::PendingIo) -> Self {
        Self {
            number: io.number,
            args: io.args,
            deadline: io.deadline,
        }
    }
}
#[derive(Default)]
pub(super) struct Stats {
    pub elapsed: Duration,
    pub before: DecodeStats,
    pub after: DecodeStats,
    pub worker: usize,
    pub instructions: u64,
    pub chunks: u64,
    pub syscalls: u64,
    pub direct_syscalls: u64,
    pub futex_wakes: u64,
    pub futex_woken: u64,
    pub serviced_syscalls: u64,
    pub service_wait: Duration,
    pub syscall_time: Duration,
    pub native: trames_accel::native::Metrics,
    pub rejected_native: Option<(u64, u64, u64)>,
    pub cpu_times: [Duration; 32],
    pub assignments: u64,
    pub waits: u64,
    pub resumes: u64,
    pub yields: u64,
    pub io_waits: u64,
    pub service_waits: u64,
    pub file_syscalls: u64,
    pub socket_syscalls: u64,
    pub timer_syscalls: u64,
    pub poll_syscalls: u64,
    pub gpu: crate::workers::GpuStats,
}
pub(super) struct Job {
    pub work: Work,
    pub stats: Stats,
    pub affinity: u64,
    pub waiting: Option<WaitState>,
    pub io: Option<trames_linux::PendingIo>,
    pub retry: Option<IoCall>,
    pub gpu_pending: Option<crate::gpu::Pending>,
}
impl Job {
    pub fn new(
        work: Work,
        affinity: u64,
        last_cpu: usize,
        waiting: Option<WaitState>,
    ) -> Box<Self> {
        let before = work.cpu.decode_stats();
        Box::new(Self {
            work,
            affinity,
            waiting,
            io: None,
            retry: None,
            gpu_pending: None,
            stats: Stats {
                before,
                after: before,
                worker: last_cpu,
                ..Default::default()
            },
        })
    }
    pub fn finish(self, outcome: Option<SliceOutcome>, peer: u64) -> Done {
        let io = self.io.as_ref().map(IoCall::from).or(self.retry);
        drop(self.io);
        let gpu_pending = self.gpu_pending.as_ref().and_then(|p| p.checkpoint(peer));
        drop(self.gpu_pending);
        let work = self.work;
        // Drop the retained View before any coordinator mapping operation.
        drop(work.memory);
        self.stats
            .finish(work.pid, work.cpu, outcome, self.waiting, io, gpu_pending)
    }
}

impl Stats {
    pub fn finish(
        self,
        pid: u32,
        cpu: trames_cpu::Cpu,
        outcome: Option<SliceOutcome>,
        waiting: Option<WaitState>,
        io: Option<IoCall>,
        gpu_pending: Option<crate::gpu::Compile>,
    ) -> Done {
        Done {
            pid,
            cpu,
            outcome,
            waiting,
            io,
            elapsed: self.elapsed,
            before: self.before,
            after: self.after,
            worker: self.worker,
            instructions: self.instructions,
            chunks: self.chunks,
            syscalls: self.syscalls,
            direct_syscalls: self.direct_syscalls,
            futex_wakes: self.futex_wakes,
            futex_woken: self.futex_woken,
            serviced_syscalls: self.serviced_syscalls,
            service_wait: self.service_wait,
            syscall_time: self.syscall_time,
            native: self.native,
            rejected_native: self.rejected_native,
            cpu_times: self.cpu_times,
            assignments: self.assignments,
            waits: self.waits,
            resumes: self.resumes,
            yields: self.yields,
            io_waits: self.io_waits,
            service_waits: self.service_waits,
            file_syscalls: self.file_syscalls,
            socket_syscalls: self.socket_syscalls,
            timer_syscalls: self.timer_syscalls,
            poll_syscalls: self.poll_syscalls,
            gpu: self.gpu,
            gpu_pending,
        }
    }
}

#[derive(Default)]
struct Queues {
    ready: VecDeque<Box<Job>>,
    parked: BTreeMap<u32, Box<Job>>,
    members: BTreeSet<u32>,
    running: usize,
    services: usize,
    finished: Vec<Done>,
    initial_slots: BTreeMap<u32, usize>,
    gpu_peak_pending: usize,
}
pub(super) struct Epoch {
    pub control: Arc<Control>,
    queues: Mutex<Queues>,
    changed: Condvar,
    pub io_target: Weak<dyn trames_vfs::IoWake>,
    deadline_origin: Instant,
    next_deadline: AtomicU64,
    ready_slots: AtomicU64,
    pub progress: AtomicU64,
}
impl Epoch {
    pub fn new(control: Arc<Control>) -> Arc<Self> {
        Arc::new_cyclic(|me: &Weak<Self>| Self {
            control,
            queues: Mutex::default(),
            changed: Condvar::new(),
            io_target: me.clone(),
            deadline_origin: Instant::now(),
            next_deadline: AtomicU64::new(u64::MAX),
            ready_slots: AtomicU64::new(0),
            progress: AtomicU64::new(0),
        })
    }
    fn deadline_ticks(&self, deadline: Instant) -> u64 {
        deadline
            .saturating_duration_since(self.deadline_origin)
            .as_nanos()
            .min((u64::MAX - 1) as u128) as u64
    }
    fn publish_deadline(&self, deadline: Option<Instant>) {
        if let Some(deadline) = deadline {
            self.next_deadline
                .fetch_min(self.deadline_ticks(deadline), Ordering::Relaxed);
        }
    }
    /// Scheduling hint only; the queue lock owns and publishes actual jobs.
    /// With no deadline, CPU/native execution does not query the host clock.
    pub fn deadline_due(&self) -> bool {
        let next = self.next_deadline.load(Ordering::Relaxed);
        next != u64::MAX && self.deadline_ticks(Instant::now()) >= next
    }
    fn refresh_ready(&self, q: &Queues) {
        let slots = q.ready.iter().fold(0, |mask, job| {
            mask | q
                .initial_slots
                .get(&job.work.pid)
                .map_or(job.affinity, |slot| 1 << slot)
        });
        self.ready_slots.store(slots, Ordering::Relaxed);
    }
    fn ready(&self, q: &mut Queues, job: Box<Job>) {
        q.ready.push_back(job);
        self.refresh_ready(q);
    }
    pub fn should_yield(&self, slot: usize, began: Instant, progressed: bool) -> bool {
        self.deadline_due()
            || (progressed
                && self.ready_slots.load(Ordering::Relaxed) & (1 << slot) != 0
                && began.elapsed() >= Duration::from_micros(250))
    }
    /// Only before launch: every View is prepared/admitted before any CPU runs.
    pub fn add(self: &Arc<Self>, mut job: Box<Job>) {
        let pid = job.work.pid;
        let mut q = self.queues.lock().unwrap();
        assert!(q.members.insert(pid), "duplicate cohort task");
        if let Some(waiting) = &job.waiting {
            waiting.wait.ticket.bind(self);
            if !waiting.wait.woken() {
                self.publish_deadline(waiting.deadline);
                q.parked.insert(pid, job);
                return;
            }
            job.work.cpu.set_x(0, 0);
            job.waiting = None;
        }
        self.ready(&mut q, job);
    }
    pub fn stop(&self) {
        let _q = self.queues.lock().unwrap();
        self.control.stop();
        self.changed.notify_all();
    }
    pub fn assign(&self, chosen: &[(u32, usize)]) {
        let mut q = self.queues.lock().unwrap();
        q.initial_slots.extend(chosen.iter().copied());
        self.refresh_ready(&q);
    }
    pub fn wake(&self, ticket: &Arc<Ticket>) -> bool {
        let mut q = self.queues.lock().unwrap();
        if self.control.stopped() || !q.members.contains(&ticket.pid) {
            return false;
        }
        if q.parked.get(&ticket.pid).is_some_and(|job| {
            job.waiting
                .as_ref()
                .is_some_and(|w| Arc::ptr_eq(&w.wait.ticket, ticket))
        }) {
            let mut job = q.parked.remove(&ticket.pid).unwrap();
            job.work.cpu.set_x(0, 0);
            job.waiting = None;
            job.stats.resumes += 1;
            self.ready(&mut q, job);
            self.changed.notify_all(); // affinity can require a particular worker
        }
        // Wake may precede park. The ticket CAS is then consumed by park below.
        true
    }
    pub fn take(&self, slot: usize) -> Option<Box<Job>> {
        let mut q = self.queues.lock().unwrap();
        loop {
            if self.control.stopped() {
                return None;
            }
            let now = Instant::now();
            let expired: Vec<_> = q
                .parked
                .iter()
                .filter_map(|(&pid, job)| {
                    (job.waiting
                        .as_ref()
                        .and_then(|w| w.deadline)
                        .or_else(|| job.io.as_ref().and_then(|io| io.wake_deadline()))
                        .is_some_and(|d| d <= now))
                    .then_some(pid)
                })
                .collect();
            for pid in expired {
                let mut job = q.parked.remove(&pid).unwrap();
                if let Some(waiting) = job.waiting.take() {
                    let value = if waiting.wait.timeout() {
                        0u64.wrapping_sub(110)
                    } else {
                        0
                    };
                    job.work.cpu.set_x(0, value);
                }
                // I/O must perform a final resource check and scatter, even on
                // timeout. Keep its pending call and do not execute SVC twice.
                job.stats.resumes += 1;
                self.ready(&mut q, job);
            }
            let deadline = q
                .parked
                .values()
                .filter_map(|j| {
                    j.waiting
                        .as_ref()
                        .and_then(|w| w.deadline)
                        .or_else(|| j.io.as_ref().and_then(|io| io.wake_deadline()))
                })
                .min();
            self.next_deadline.store(
                deadline.map_or(u64::MAX, |d| self.deadline_ticks(d)),
                Ordering::Relaxed,
            );
            let preferred = q
                .ready
                .iter()
                .position(|j| q.initial_slots.get(&j.work.pid) == Some(&slot));
            let available = preferred.or_else(|| {
                q.ready.iter().position(|j| {
                    j.affinity & (1 << slot) != 0 && !q.initial_slots.contains_key(&j.work.pid)
                })
            });
            if let Some(i) = available {
                let mut job = q.ready.remove(i).unwrap();
                self.refresh_ready(&q);
                let work = &mut job.work;
                q.initial_slots.remove(&work.pid);
                if job.stats.worker != slot {
                    work.cpu.clear_exclusive();
                }
                job.stats.worker = slot;
                if let Some(snapshot) = &mut work.syscalls {
                    snapshot.processor = slot as u32;
                }
                q.running += 1;
                return Some(job);
            }
            if q.running == 0
                && q.services == 0
                && q.ready.is_empty()
                && deadline.is_none()
                && !q.parked.values().any(|j| j.gpu_pending.is_some())
            {
                self.control.stop();
                self.changed.notify_all();
                return None;
            }
            q = if let Some(deadline) = deadline {
                self.changed
                    .wait_timeout(q, deadline.saturating_duration_since(Instant::now()))
                    .unwrap()
                    .0
            } else {
                self.changed.wait(q).unwrap()
            };
        }
    }
    pub fn park(&self, mut job: Box<Job>, waiting: Option<WaitState>) {
        let mut q = self.queues.lock().unwrap();
        q.running -= 1;
        let pid = job.work.pid;
        if let Some(waiting) = waiting {
            job.stats.waits += 1;
            if waiting.wait.woken() {
                job.work.cpu.set_x(0, 0);
                job.stats.resumes += 1;
                self.ready(&mut q, job);
            } else {
                self.publish_deadline(waiting.deadline);
                job.waiting = Some(waiting);
                assert!(q.parked.insert(pid, job).is_none());
            }
        } else {
            job.stats.yields += 1;
            self.ready(&mut q, job);
        }
        self.changed.notify_all();
    }
    pub fn park_io(&self, mut job: Box<Job>, pending: trames_linux::PendingIo) {
        let mut q = self.queues.lock().unwrap();
        q.running -= 1;
        job.stats.io_waits += 1;
        let ready = pending.signal.ready();
        job.io = Some(pending);
        if ready {
            job.stats.resumes += 1;
            self.ready(&mut q, job);
        } else {
            self.publish_deadline(job.io.as_ref().and_then(|io| io.wake_deadline()));
            assert!(q.parked.insert(job.work.pid, job).is_none());
        }
        self.changed.notify_all();
    }
    pub fn park_gpu(&self, mut job: Box<Job>, pending: crate::gpu::Pending) {
        let mut q = self.queues.lock().unwrap();
        q.running -= 1;
        job.stats.gpu.waits += 1;
        let ready = pending.signal().ready();
        job.gpu_pending = Some(pending);
        if ready {
            job.stats.resumes += 1;
            self.ready(&mut q, job);
        } else {
            assert!(q.parked.insert(job.work.pid, job).is_none());
            let count = q
                .parked
                .values()
                .filter(|j| j.gpu_pending.as_ref().is_some_and(|p| p.compile.is_some()))
                .count();
            q.gpu_peak_pending = q.gpu_peak_pending.max(count);
        }
        self.changed.notify_all();
    }
    pub fn gpu_peak_pending(&self) -> usize {
        self.queues.lock().unwrap().gpu_peak_pending
    }
    #[cfg(test)]
    pub fn await_gpu_park(&self, pid: u32) {
        let deadline = Instant::now() + Duration::from_secs(2);
        let mut q = self.queues.lock().unwrap();
        while !q.parked.get(&pid).is_some_and(|j| j.gpu_pending.is_some()) {
            assert!(!self.control.stopped(), "cohort stopped before GPU park");
            let left = deadline.saturating_duration_since(Instant::now());
            assert!(!left.is_zero(), "GPU did not park");
            q = self.changed.wait_timeout(q, left).unwrap().0;
        }
    }
    /// Transfer an execution slot to a request before publishing it. A queued
    /// service prevents quiescence even when every worker has run out of work.
    pub fn start_service(&self) {
        let mut q = self.queues.lock().unwrap();
        q.running -= 1;
        q.services += 1;
        self.changed.notify_all();
    }
    pub fn pending_services(&self) -> usize {
        self.queues.lock().unwrap().services
    }
    /// None checkpoints before effects; Err means an abandoned/panicked request.
    /// Completed effects must commit even when stop raced with their completion.
    pub fn end_service(&self, mut job: Box<Job>, value: Result<Option<u64>, ()>) {
        let mut q = self.queues.lock().unwrap();
        q.services -= 1;
        match value {
            Ok(Some(value)) => {
                job.work.cpu.set_x(0, value);
                job.stats.serviced_syscalls += 1;
                job.stats.resumes += 1;
                self.ready(&mut q, job);
            }
            value => {
                let outcome = value
                    .ok()
                    .map(|_| SliceOutcome::Syscall { instructions: 0 });
                q.finished
                    .push(job.finish(outcome, self.progress.load(Ordering::Relaxed)));
                self.control.stop();
            }
        }
        self.changed.notify_all();
    }
    pub fn finish(&self, done: Done) {
        let mut q = self.queues.lock().unwrap();
        q.running -= 1;
        q.finished.push(done);
        self.control.stop();
        self.changed.notify_all();
    }
    /// All launched workers must have acknowledged stop before this call.
    pub fn drain(&self) -> Vec<Done> {
        let mut q = self.queues.lock().unwrap();
        assert_eq!(q.running, 0);
        assert_eq!(q.services, 0, "in-flight service still owns a View");
        let mut done = std::mem::take(&mut q.finished);
        done.extend(q.ready.drain(..).map(|job| {
            job.finish(
                Some(SliceOutcome::Yield { instructions: 0 }),
                self.progress.load(Ordering::Relaxed),
            )
        }));
        done.extend(std::mem::take(&mut q.parked).into_values().map(|job| {
            job.finish(
                Some(SliceOutcome::Yield { instructions: 0 }),
                self.progress.load(Ordering::Relaxed),
            )
        }));
        done
    }
}

impl trames_vfs::IoWake for Epoch {
    fn wake_io(&self, signal: &Arc<trames_vfs::IoSignal>) {
        let mut q = self.queues.lock().unwrap();
        if self.control.stopped() {
            return;
        }
        if q.parked.get(&signal.pid).is_some_and(|job| {
            job.io
                .as_ref()
                .is_some_and(|io| Arc::ptr_eq(&io.signal, signal))
                || job
                    .gpu_pending
                    .as_ref()
                    .is_some_and(|gpu| Arc::ptr_eq(gpu.signal(), signal))
        }) {
            let mut job = q.parked.remove(&signal.pid).unwrap();
            job.stats.resumes += 1;
            self.ready(&mut q, job);
            self.changed.notify_all();
        }
        // If still running, park_io consumes the signal's fired bit under this
        // same queue lock. Notifications never access a guest Memory or CPU.
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use trames_memory::{Memory, Permissions, PAGE_SIZE};
    fn job(pid: u32, epoch: &Arc<Epoch>) -> Box<Job> {
        let mut memory = Memory::default();
        memory.map(0x1000, PAGE_SIZE, Permissions::RW).unwrap();
        Job::new(
            Work {
                gpu: None,
                pid,
                cpu: trames_cpu::Cpu::default(),
                memory: Some(memory.parallel_view(&mut Default::default()).unwrap()),
                control: epoch.control.clone(),
                syscalls: None,
            },
            1,
            0,
            None,
        )
    }
    #[test]
    fn io_wake_before_park_and_stop_preserve_pending_call_and_release_subscriptions() {
        let mut vfs = trames_vfs::Vfs::default();
        let [read, write] = vfs.pipe(0).unwrap();
        let direct = trames_linux::DirectSyscalls::new(vfs.direct_descriptors());
        let epoch = Epoch::new(Arc::new(Control::new(100)));
        epoch.add(job(1, &epoch));
        let mut running = epoch.take(0).unwrap();
        let args = [read as u64, 0x1000, 1, 0, 0, 0];
        let attempt = direct
            .start(
                63,
                args,
                running.work.memory.as_mut().unwrap(),
                1,
                &epoch.io_target,
                None,
            )
            .unwrap();
        let trames_linux::IoAttempt::Blocked(pending) = attempt else {
            panic!("expected wait");
        };
        // Wake while the task is running and has not yet entered parked.
        vfs.write(write, b"x").unwrap();
        assert!(pending.signal.ready());
        epoch.park_io(running, pending);
        let mut running = epoch.take(0).unwrap();
        assert_eq!(running.stats.resumes, 1);
        let pending = running.io.take().unwrap();
        assert!(matches!(
            direct.resume(pending, running.work.memory.as_mut().unwrap()),
            trames_linux::IoAttempt::Complete(Ok(1))
        ));
        let attempt = direct
            .start(
                63,
                args,
                running.work.memory.as_mut().unwrap(),
                1,
                &epoch.io_target,
                None,
            )
            .unwrap();
        let trames_linux::IoAttempt::Blocked(pending) = attempt else {
            panic!("expected wait");
        };
        let signal = Arc::downgrade(&pending.signal);
        epoch.park_io(running, pending);
        epoch.stop();
        let done = epoch.drain();
        assert_eq!(done[0].io.as_ref().unwrap().args, args);
        assert!(signal.upgrade().is_none());
        // Old callbacks cannot resurrect a stopped cohort or retain a View.
        vfs.write(write, b"y").unwrap();
        assert!(epoch.take(0).is_none());
    }
    #[test]
    fn wake_before_park_is_retained_and_cancellation_drops_blocked_leases() {
        let table = crate::futex::Futexes::default();
        let epoch = Epoch::new(Arc::new(Control::new(100)));
        epoch.add(job(1, &epoch));
        let running = epoch.take(0).unwrap();
        let wait = table
            .wait_target((0, 1, 0x1000), 1, 1, Some(Arc::downgrade(&epoch)), || {
                Ok::<_, ()>(())
            })
            .unwrap();
        assert_eq!(table.wake((0, 1, 0x1000), 1, 1), 1);
        assert!(!epoch.control.stopped());
        epoch.park(
            running,
            Some(WaitState {
                wait,
                deadline: None,
            }),
        );
        let running = epoch.take(0).unwrap();
        assert_eq!(running.stats.resumes, 1);
        let wait = table
            .wait_target((0, 1, 0x1000), 1, 1, Some(Arc::downgrade(&epoch)), || {
                Ok::<_, ()>(())
            })
            .unwrap();
        epoch.park(
            running,
            Some(WaitState {
                wait,
                deadline: None,
            }),
        );
        epoch.stop();
        let drained = epoch.drain();
        assert_eq!(drained.len(), 1);
        assert!(drained[0].waiting.is_some());
        drop(drained);
        assert_eq!(table.wake((0, 1, 0x1000), 1, 1), 0);
    }
    #[test]
    fn expired_wait_resumes_on_eligible_cpu_without_a_coordinator() {
        let table = crate::futex::Futexes::default();
        let epoch = Epoch::new(Arc::new(Control::new(100)));
        let wait = table
            .wait((0, 1, 0x1000), 1, 1, || Ok::<_, ()>(()))
            .unwrap();
        let mut task = job(1, &epoch);
        task.affinity = 2;
        task.waiting = Some(WaitState {
            wait,
            deadline: Some(Instant::now() + Duration::from_millis(1)),
        });
        epoch.add(task);
        // A worker sleeps until the actual deadline; no polling retry loop.
        let task = epoch.take(1).unwrap();
        assert_eq!(task.work.cpu.x(0), 0u64.wrapping_sub(110));
        assert_eq!(task.stats.resumes, 1);
        assert_eq!(task.stats.worker, 1);
        epoch.finish(task.finish(Some(SliceOutcome::Yield { instructions: 0 }), 0));
        assert_eq!(epoch.drain().len(), 1);
        assert_eq!(table.wake((0, 1, 0x1000), 1, 1), 0);
    }
    #[test]
    fn initial_assignment_keeps_a_flexible_task_out_of_a_pinned_tasks_slot() {
        let epoch = Epoch::new(Arc::new(Control::new(100)));
        let mut flexible = job(1, &epoch);
        flexible.affinity = 3;
        epoch.add(flexible);
        epoch.add(job(2, &epoch));
        epoch.assign(&[(1, 1), (2, 0)]);
        let pinned = epoch.take(0).unwrap();
        let flexible = epoch.take(1).unwrap();
        assert_eq!(pinned.work.pid, 2);
        assert_eq!(flexible.work.pid, 1);
        epoch.finish(pinned.finish(Some(SliceOutcome::Yield { instructions: 0 }), 0));
        epoch.finish(flexible.finish(Some(SliceOutcome::Yield { instructions: 0 }), 0));
        assert_eq!(epoch.drain().len(), 2);
    }
    #[test]
    fn stop_notifies_an_idle_worker_and_returns_every_view() {
        let epoch = Epoch::new(Arc::new(Control::new(100)));
        let table = crate::futex::Futexes::default();
        let mut task = job(1, &epoch);
        task.waiting = Some(WaitState {
            wait: table
                .wait((0, 1, 0x1000), 1, 1, || Ok::<_, ()>(()))
                .unwrap(),
            deadline: Some(Instant::now() + Duration::from_secs(3600)),
        });
        epoch.add(task);
        let peer = epoch.clone();
        let thread = std::thread::spawn(move || assert!(peer.take(0).is_none()));
        epoch.stop();
        thread.join().unwrap();
        assert_eq!(epoch.drain().len(), 1);
    }
}
