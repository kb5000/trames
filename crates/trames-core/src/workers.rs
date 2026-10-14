// SPDX-License-Identifier: GPL-3.0-or-later
//! CPU workers retain state and run until an event or a requested checkpoint.
//! Checked pure NativeCall runs on workers; Linux/VFS stay on the coordinator.
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
    pub native: trames_accel::native::Metrics,
    pub rejected_native: Option<(u64, u64, u64)>,
}
/// Native scopes begin after stub validation; even unwinding releases the count.
struct NativeScope<'a>(&'a AtomicUsize);
impl Drop for NativeScope<'_> {
    fn drop(&mut self) {
        self.0.fetch_sub(1, Ordering::Relaxed);
    }
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
    pub peak_native: Arc<AtomicUsize>,
    active_native: Arc<AtomicUsize>,
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
            peak_native: Arc::new(AtomicUsize::new(0)),
            active_native: Arc::new(AtomicUsize::new(0)),
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
        let join = thread::Builder::new()
            .name(format!("trames-cpu-{index}"))
            .spawn(move || {
                let mut registry = trames_accel::native::Registry::default();
                while let Ok(mut work) = receive.recv() {
                    let before = work.cpu.decode_stats();
                    let began = Instant::now();
                    let mut instructions = 0;
                    let mut chunks = 0;
                    let mut running = false;
                    let mut rejected_native = None;
                    let mut credit = 0;
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
                            credit -= retired;
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
                                    &mut work.memory,
                                    *pc,
                                    *module,
                                    *function,
                                    args,
                                    || {
                                        let n = active_native.fetch_add(1, Ordering::Relaxed) + 1;
                                        peak_native.fetch_max(n, Ordering::Relaxed);
                                        NativeScope(&active_native)
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
                            native: {
                                registry.metrics.worker_calls = registry.metrics.calls;
                                std::mem::take(&mut registry.metrics)
                            },
                            rejected_native,
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
    use std::{
        cell::{Cell, RefCell},
        rc::Rc,
    };
    use trames_memory::{Memory, MemoryError, NativeMemory, PageBacking, Permissions, PAGE_SIZE};

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
        }
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
