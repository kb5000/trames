// SPDX-License-Identifier: GPL-3.0-or-later
//! Persistent CPU-only workers. The coordinator owns Linux, VFS and NativeCall.
use std::{
    sync::{
        atomic::{AtomicUsize, Ordering},
        mpsc, Arc, Barrier,
    },
    thread,
    time::{Duration, Instant},
};
use trames_cpu::{Cpu, DecodeStats, SliceOutcome};
use trames_memory::parallel::View;

pub(super) struct Work {
    pub pid: u32,
    pub cpu: Cpu,
    pub memory: View,
    pub budget: u64,
    pub start: Arc<Barrier>,
}
pub(super) struct Done {
    pub pid: u32,
    pub cpu: Cpu,
    pub outcome: Option<SliceOutcome>,
    pub elapsed: Duration,
    pub before: DecodeStats,
    pub after: DecodeStats,
    pub worker: usize,
}
struct Worker {
    send: mpsc::Sender<Work>,
    join: thread::JoinHandle<()>,
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
                    work.start.wait();
                    let n = active.fetch_add(1, Ordering::Relaxed) + 1;
                    peak.fetch_max(n, Ordering::Relaxed);
                    let before = work.cpu.decode_stats();
                    let began = Instant::now();
                    let outcome = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                        work.cpu.run_slice(&mut work.memory, work.budget)
                    }))
                    .ok();
                    let elapsed = began.elapsed();
                    let after = work.cpu.decode_stats();
                    active.fetch_sub(1, Ordering::Relaxed);
                    if done
                        .send(Done {
                            pid: work.pid,
                            cpu: work.cpu,
                            outcome,
                            elapsed,
                            before,
                            after,
                            worker: index,
                        })
                        .is_err()
                    {
                        break;
                    }
                }
            })?;
        self.workers[index] = Some(Worker { send, join });
        Ok(())
    }
    pub fn dispatch(&self, index: usize, work: Work) -> Result<(), ()> {
        self.workers[index]
            .as_ref()
            .ok_or(())?
            .send
            .send(work)
            .map_err(|_| ())
    }
    pub fn complete(&self) -> Result<Done, ()> {
        self.receive.recv().map_err(|_| ())
    }
}
impl Drop for Pool {
    fn drop(&mut self) {
        for worker in self.workers.iter_mut().filter_map(Option::take) {
            drop(worker.send);
            let _ = worker.join.join();
        }
    }
}
