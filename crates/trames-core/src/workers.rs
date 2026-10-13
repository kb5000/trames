// SPDX-License-Identifier: GPL-3.0-or-later
//! CPU workers retain state and run until an event or a requested checkpoint.
//! The coordinator owns Linux, VFS and NativeCall; no kernel objects cross threads.
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
            .fetch_update(Ordering::Relaxed, Ordering::Relaxed, |left| {
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
}
struct Worker {
    send: mpsc::Sender<Work>,
    join: thread::JoinHandle<()>,
    control: Option<Arc<Control>>,
}
pub(super) struct Pool {
    workers: Vec<Option<Worker>>,
    send: mpsc::Sender<Done>,
    receive: mpsc::Receiver<Done>,
    active: Arc<AtomicUsize>,
    pub peak: Arc<AtomicUsize>,
}
impl Pool {
    pub fn new(count: usize) -> Self {
        let (send, receive) = mpsc::channel();
        Self {
            workers: (0..count).map(|_| None).collect(),
            send,
            receive,
            active: Arc::new(AtomicUsize::new(0)),
            peak: Arc::new(AtomicUsize::new(0)),
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
        let join = thread::Builder::new()
            .name(format!("trames-cpu-{index}"))
            .spawn(move || {
                while let Ok(mut work) = receive.recv() {
                    let before = work.cpu.decode_stats();
                    let began = Instant::now();
                    let mut instructions = 0;
                    let mut chunks = 0;
                    let mut running = false;
                    let outcome = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                        loop {
                            let Some(budget) = work.control.claim() else {
                                break SliceOutcome::Yield { instructions: 0 };
                            };
                            if !running {
                                let n = active.fetch_add(1, Ordering::Relaxed) + 1;
                                peak.fetch_max(n, Ordering::Relaxed);
                                running = true;
                            }
                            let result = work.cpu.run_slice(&mut work.memory, budget);
                            chunks += 1;
                            let retired = match &result {
                                SliceOutcome::Fault(_) => work.cpu.fault_retired(),
                                SliceOutcome::Yield { instructions }
                                | SliceOutcome::Syscall { instructions }
                                | SliceOutcome::NativeCall { instructions, .. } => *instructions,
                            };
                            instructions += retired;
                            // A pending native trap needs its own budget credit.
                            // Its retirement is committed by the coordinator, not
                            // counted here; peers cannot spend the same credit.
                            let charged = retired
                                + u64::from(matches!(result, SliceOutcome::NativeCall { .. }));
                            work.control
                                .remaining
                                .fetch_add(budget - charged, Ordering::Relaxed);
                            if !matches!(result,
                                SliceOutcome::Yield { instructions } if instructions == budget
                            ) {
                                break result;
                            }
                        }
                    }))
                    .ok();
                    // The first syscall/native/fault/yield requests a checkpoint
                    // immediately, before the coordinator receives this message.
                    work.control.stop();
                    let elapsed = began.elapsed();
                    let after = work.cpu.decode_stats();
                    if running {
                        active.fetch_sub(1, Ordering::Relaxed);
                    }
                    if done
                        .send(Done {
                            pid: work.pid,
                            cpu: work.cpu,
                            outcome,
                            elapsed,
                            before,
                            after,
                            worker: index,
                            instructions,
                            chunks,
                        })
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
        worker.control = Some(work.control.clone());
        worker.send.send(work).map_err(|_| ())
    }
    pub fn complete(&self) -> Result<Done, ()> {
        self.receive.recv().map_err(|_| ())
    }
    pub fn complete_timeout(&self, timeout: Duration) -> Result<Option<Done>, ()> {
        match self.receive.recv_timeout(timeout) {
            Ok(done) => Ok(Some(done)),
            Err(mpsc::RecvTimeoutError::Timeout) => Ok(None),
            Err(mpsc::RecvTimeoutError::Disconnected) => Err(()),
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
        for worker in self.workers.iter_mut().filter_map(Option::take) {
            drop(worker.send);
            let _ = worker.join.join();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use trames_memory::{Memory, Permissions, PAGE_SIZE};

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
        }
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
    fn native_trap_reserves_its_retirement_and_stops_queued_peers() {
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
            Some(SliceOutcome::NativeCall {
                instructions: 0,
                ..
            })
        ));
        assert_eq!(done.instructions, 0);
        assert_eq!(control.remaining.load(Ordering::Relaxed), 0);
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
