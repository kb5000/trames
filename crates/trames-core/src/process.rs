// SPDX-License-Identifier: GPL-3.0-or-later
//! Virtual processes and continuous parallel CPU execution. Guest fork/exec stay virtual.
use super::{Exit, Runtime, RuntimeError};
use crate::terminal::Terminal;
use std::{
    cell::{Cell, RefCell},
    collections::BTreeMap,
    rc::Rc,
    time::{Duration, Instant},
};
use trames_cpu::{Cpu, SliceOutcome};
use trames_linux::{GuestOutput, Linux, OutputFd, SyscallOutcome, TerminalEvent, MAX_WRITE};
use trames_memory::{Access, Memory};
use trames_vfs::capabilities;
use trames_vfs::{Errno, FdKind, AT_FDCWD, MAX_FDS, O_NONBLOCK};

const MAX_PROCESSES: usize = trames_vfs::MAX_TASKS;
#[cfg(test)]
const TOTAL_MAPPED_LIMIT: usize = 1024 * 1024 * 1024;
const UNBLOCKABLE: u64 = (1 << 8) | (1 << 18);
/// Preserve round-robin queue priority while relocating an earlier flexible
/// task when a later pinned task needs its slot. Mask width is at most 32 CPUs.
fn assign_cpus(ready: impl IntoIterator<Item = (u32, u64)>, count: usize) -> Vec<(u32, usize)> {
    fn place(pid: u32, mask: u64, slots: &mut [Option<(u32, u64)>], visited: &mut u64) -> bool {
        let eligible = mask & !*visited;
        // Equal/full masks take the same lowest-free-slot fast path as before.
        if let Some(slot) = slots
            .iter()
            .enumerate()
            .find_map(|(i, p)| (eligible & (1 << i) != 0 && p.is_none()).then_some(i))
        {
            slots[slot] = Some((pid, mask));
            return true;
        }
        for slot in 0..slots.len() {
            if eligible & (1 << slot) == 0 {
                continue;
            }
            *visited |= 1 << slot;
            let (previous, previous_mask) = slots[slot].expect("occupied eligible slot");
            if place(previous, previous_mask, slots, visited) {
                slots[slot] = Some((pid, mask));
                return true;
            }
        }
        false
    }
    let mut slots = vec![None; count];
    let mut selected = Vec::new();
    for (pid, mask) in ready {
        if place(pid, mask, &mut slots, &mut 0) {
            selected.push(pid);
            if selected.len() == count {
                break;
            }
        }
    }
    selected
        .into_iter()
        .map(|pid| {
            let slot = slots
                .iter()
                .position(|p| p.is_some_and(|p| p.0 == pid))
                .expect("assigned task");
            (pid, slot)
        })
        .collect()
}
#[path = "process_caps.rs"]
mod caps;
#[path = "process_compat.rs"]
mod compat;
#[path = "process_events.rs"]
mod events;
#[path = "process_signals.rs"]
mod signals;
use signals::Pending;
#[path = "process_pid.rs"]
mod pid_namespace;
use compat::AltStack;
#[path = "process_scheduling.rs"]
mod scheduling;
#[path = "process_seccomp.rs"]
mod seccomp;
#[path = "process_service.rs"]
mod service;
use service::DiskEpoch;
#[derive(Clone, Copy, Default)]
struct Action {
    handler: u64,
    flags: u64,
    restorer: u64,
    mask: u64,
}
#[derive(Clone, Copy)]
struct Info {
    sender: u32,
    uid: u32,
    status: i32,
    code: i32,
    timer: Option<(i32, i32, u64)>,
    fault: Option<u64>,
}
#[derive(Clone)]
enum State {
    Ready,
    Gpu {
        job: u64,
        pc: u64,
        operation: u64,
        instructions: u64,
        began: Instant,
    },
    Blocked(u32, [u64; 6]),
    Sleep(Instant, u64),
    Suspend(u64),
    Vfork(u32),
    Futex {
        address: u64,
        key: (u64, u64, u64),
        bits: u32,
        deadline: Option<Instant>,
    },
    Sigwait {
        mask: u64,
        info: u64,
        deadline: Option<Instant>,
    },
    Stopped,
    Zombie(i32),
}
struct Process {
    cpu: Cpu,
    affinity: u64,
    last_cpu: usize,
    nice: i32,
    scheduler: u32,
    memory: Memory,
    linux: Linux,
    parent: u32,
    parent_thread: u32,
    pdeath_signal: u32,
    group: u32,
    session: u32,
    state: State,
    actions: [Action; 65],
    mask: u64,
    pending: Pending,
    shared_pending: Rc<RefCell<Pending>>,
    frames: u32,
    wait_event: Option<i32>,
    poll_deadline: Option<Instant>,
    socket_deadline: Option<Instant>,
    poll_mask: Option<u64>,
    pending_open: Option<u32>,
    stopped_state: Option<Box<State>>,
    auto_reap: bool,
    tgid: u32,
    started: Instant,
    cpu_time: Duration,
    vm_id: u64,
    files_id: u64,
    fs_id: u64,
    actions_id: u64,
    rekey: Option<u32>,
    subreaper: bool,
    alt_stack: AltStack,
    membarrier_registered: Rc<Cell<bool>>,
    file_size_limit: Rc<Cell<(u64, u64)>>,
    oom: Rc<trames_vfs::OomTask>,
    seccomp: seccomp::Seccomp,
}
impl Process {
    fn frozen(&self, pid: u32) -> bool {
        self.linux.vfs.cgroup_frozen(pid) && self.pending_bits() & (1 << 8) == 0
    }
    fn fork(&self) -> Self {
        Self {
            cpu: self.cpu.clone(),
            affinity: self.affinity,
            last_cpu: self.last_cpu,
            nice: self.nice,
            scheduler: self.scheduler,
            memory: self.memory.clone(),
            linux: self.linux.clone(),
            parent: 0,
            parent_thread: 0,
            pdeath_signal: 0,
            group: self.group,
            session: self.session,
            state: State::Ready,
            actions: self.actions,
            mask: self.mask,
            pending: Pending::default(),
            shared_pending: Rc::new(RefCell::new(Pending::default())),
            frames: self.frames,
            wait_event: None,
            poll_deadline: None,
            socket_deadline: None,
            poll_mask: None,
            pending_open: None,
            stopped_state: None,
            auto_reap: false,
            tgid: self.tgid,
            started: Instant::now(),
            cpu_time: Duration::ZERO,
            vm_id: self.vm_id,
            files_id: self.files_id,
            fs_id: self.fs_id,
            actions_id: self.actions_id,
            rekey: None,
            subreaper: false,
            alt_stack: self.alt_stack,
            membarrier_registered: Rc::new(Cell::new(self.membarrier_registered.get())),
            file_size_limit: Rc::new(Cell::new(self.file_size_limit.get())),
            oom: Rc::new(self.oom.fork()),
            seccomp: self.seccomp.clone(),
        }
    }
}
struct ProcMaps {
    identity: (u64, u64),
    bytes: Vec<u8>,
}
struct Kernel {
    memory_limits: super::MemoryLimits,
    cpu_count: usize,
    workers: crate::workers::Pool,
    parallel_memory: trames_memory::parallel::Registry,
    parallel_rounds: u64,
    worker_slices: u64,
    worker_chunks: u64,
    worker_syscalls: u64,
    service_requests: u64,
    disk_reads: u64,
    disk_read_bytes: u64,
    service_write_buffer: Vec<u8>,
    serviced_syscalls: u64,
    service_wait: Duration,
    parallel_memory_time: Duration,
    parallel_dispatch_time: Duration,
    parallel_stop_time: Duration,
    next_parallel_pid: u32,
    per_cpu: Vec<Duration>,
    processes: BTreeMap<u32, Process>,
    next_pid: u32,
    terminal: Option<Terminal>,
    started: Instant,
    instructions: u64,
    boot_time: u64,
    context_switches: u64,
    decode_lookups: u64,
    instructions_decoded: u64,
    decode_misses: u64,
    cpu_total: Duration,
    syscall_total: Duration,
    syscalls: u64,
    proc_refreshes: u64,
    proc_maps: RefCell<BTreeMap<u64, ProcMaps>>,
    profile_syscalls: bool,
    idle: Duration,
    load: [f64; 3],
    load_sample: Instant,
    next_resource: u64,
    timers: BTreeMap<i32, events::Timer>,
    next_timer: i32,
    native: trames_accel::native::Registry,
    gpu: RefCell<BTreeMap<u64, trames_vulkan::Backend>>,
    gpu_budget: trames_vulkan::Budget,
    gpu_display: trames_vulkan::Display,
    gpu_compiles: u64,
    gpu_peak_pending: usize,
    gpu_peer_instructions: u64,
    gpu_wait_time: Duration,
    gpu_transfers: trames_vulkan::TransferMetrics,
}
fn error(e: Errno) -> u64 {
    0u64.wrapping_sub(e.0 as u64)
}
fn accelerated(p: &Process, key: u64, profile: u64) -> bool {
    p.linux
        .auxiliary
        .chunks_exact(16)
        .any(|pair| pair[..8] == key.to_le_bytes() && pair[8..] == profile.to_le_bytes())
}
fn args(cpu: &Cpu) -> [u64; 6] {
    std::array::from_fn(|i| cpu.x(i))
}
fn put_args(cpu: &mut Cpu, a: [u64; 6]) {
    for (i, value) in a.into_iter().enumerate() {
        cpu.set_x(i, value);
    }
}
fn read_u32(m: &Memory, a: u64) -> Result<u32, Errno> {
    let mut b = [0; 4];
    m.read(a, &mut b).map_err(|_| Errno(14))?;
    Ok(u32::from_le_bytes(b))
}
fn read_u64(m: &Memory, a: u64) -> Result<u64, Errno> {
    m.read_u64(a).map_err(|_| Errno(14))
}
fn write_u64(m: &mut Memory, a: u64, v: u64) -> Result<(), Errno> {
    m.write(a, &v.to_le_bytes()).map_err(|_| Errno(14))
}
fn write_u32(m: &mut Memory, a: u64, v: u32) -> Result<(), Errno> {
    m.write(a, &v.to_le_bytes()).map_err(|_| Errno(14))
}
fn strings(m: &Memory, address: u64) -> Result<Vec<Vec<u8>>, Errno> {
    if address == 0 {
        return Ok(Vec::new());
    }
    let mut out = Vec::new();
    let mut total = 0;
    for i in 0..4096 {
        let a = read_u64(m, address.checked_add(i * 8).ok_or(Errno(14))?)?;
        if a == 0 {
            return Ok(out);
        }
        let value = m.read_c_string(a, 65536).map_err(|_| Errno(14))?;
        total += value.len() + 1;
        if total > 65536 {
            return Err(Errno(7));
        }
        out.push(value);
    }
    Err(Errno(7))
}
impl Kernel {
    fn native_call(
        &mut self,
        p: &mut Process,
        pc: u64,
        module: u64,
        function: u64,
        a: [u64; 3],
    ) -> Result<Option<trames_accel::native::Return>, trames_accel::native::Error> {
        if module != trames_vulkan::PROFILE {
            return self
                .native
                .invoke(&mut p.memory, pc, module, function, a)
                .map(Some);
        }
        if !accelerated(p, trames_vulkan::AUXV_KEY, trames_vulkan::PROFILE) {
            return Err(trames_accel::native::Error::InvalidEntry);
        }
        self.native
            .authorize_stateful(&p.memory, pc, module, function)?;
        let value = {
            let mut gpu = self.gpu.borrow_mut();
            let backend = gpu.entry(p.vm_id).or_insert_with(|| {
                trames_vulkan::Backend::with_budget(self.gpu_budget.clone())
                    .with_display(self.gpu_display.clone())
                    .with_wayland(p.linux.vfs.wayland_server())
            });
            if matches!(a[0], 110 | 111) && a[2] == 128 {
                let address = a[1]
                    .checked_add(8)
                    .ok_or(trames_accel::native::Error::InvalidEntry)?;
                let fd = p
                    .memory
                    .read_u64(address)
                    .map_err(trames_accel::native::Error::Memory)?;
                let authenticated = u32::try_from(fd)
                    .ok()
                    .and_then(|fd| p.linux.vfs.wayland_connection(fd).ok());
                backend.authenticate_wayland(authenticated);
            }
            let before = backend.transfers;
            let result = backend.invoke(&mut p.memory, a[0], a[1], a[2]);
            self.gpu_transfers.upload_bytes += backend.transfers.upload_bytes - before.upload_bytes;
            self.gpu_transfers.download_bytes +=
                backend.transfers.download_bytes - before.download_bytes;
            self.gpu_transfers.direct_bytes += backend.transfers.direct_bytes - before.direct_bytes;
            self.gpu_transfers.buffered_bytes +=
                backend.transfers.buffered_bytes - before.buffered_bytes;
            result.map_err(trames_accel::native::Error::Memory)?
        };
        let value = match value {
            trames_vulkan::Invocation::Complete(value) => value,
            trames_vulkan::Invocation::Pending(job) => {
                self.gpu_compiles += 1;
                let pending = 1 + self
                    .processes
                    .values()
                    .filter(|other| matches!(other.state, State::Gpu { .. }))
                    .count();
                self.gpu_peak_pending = self.gpu_peak_pending.max(pending);
                p.state = State::Gpu {
                    job,
                    pc,
                    operation: a[0],
                    instructions: self.instructions,
                    began: Instant::now(),
                };
                return Ok(None);
            }
        };
        if (value as i64) < 0 {
            p.linux.vfs.log(
                4,
                &format!(
                    "pid={} Vulkan op={} result={} pc={pc:#x} comm={:?}",
                    p.tgid,
                    a[0],
                    value as i64,
                    String::from_utf8_lossy(&p.linux.command_name)
                ),
            );
        }
        Ok(Some(trames_accel::native::Return {
            value,
            fallback: false,
        }))
    }
    fn resume_gpu(&mut self, pid: u32, p: &mut Process) -> bool {
        let State::Gpu {
            job,
            pc,
            operation,
            instructions,
            began,
        } = p.state
        else {
            return false;
        };
        let result = self
            .gpu
            .borrow_mut()
            .get_mut(&p.vm_id)
            .map_or(Ok(Some((-4i64) as u64)), |backend| {
                backend.poll(&mut p.memory, job)
            });
        if matches!(result, Ok(None)) {
            return false;
        }
        self.gpu_peer_instructions += self.instructions.saturating_sub(instructions);
        self.gpu_wait_time += began.elapsed();
        p.state = State::Ready;
        match result {
            Ok(Some(value)) => {
                if (value as i64) < 0 {
                    p.linux.vfs.log(
                        4,
                        &format!(
                            "pid={} Vulkan op={operation} result={} pc={pc:#x} comm={:?}",
                            p.tgid,
                            value as i64,
                            String::from_utf8_lossy(&p.linux.command_name)
                        ),
                    );
                }
                p.cpu.set_x(0, value);
                p.cpu.set_x(1, 0);
                p.cpu.pc = pc + 4;
                self.instructions += 1;
            }
            Err(error) => {
                self.synchronous_fault(pid, p, &trames_cpu::CpuFault::Memory { pc, error })
            }
            Ok(None) => unreachable!(),
        }
        true
    }
    fn resource_id(&mut self) -> u64 {
        let id = self.next_resource;
        self.next_resource += 1;
        id
    }
    fn other_memory(&self, vm_id: u64) -> usize {
        let mut spaces = BTreeMap::new();
        for p in self
            .processes
            .values()
            .filter(|p| p.vm_id != vm_id && !matches!(p.state, State::Zombie(_)))
        {
            spaces.entry(p.vm_id).or_insert(p.memory.mapped_bytes());
        }
        spaces.values().sum()
    }
    fn sync_resources(&mut self, p: &Process) {
        if matches!(p.state, State::Zombie(_)) {
            return;
        }
        for other in self
            .processes
            .values_mut()
            .filter(|o| !matches!(o.state, State::Zombie(_)))
        {
            if other.vm_id == p.vm_id {
                other.linux.share_heap_from(&p.linux);
            }
            if other.files_id == p.files_id {
                other.linux.vfs.share_descriptors_from(&p.linux.vfs);
            }
            if other.fs_id == p.fs_id {
                other.linux.vfs.share_context_from(&p.linux.vfs);
            }
            if other.actions_id == p.actions_id {
                other.actions = p.actions;
            }
        }
    }
    fn process_maps(&self, p: &Process) -> Vec<u8> {
        // Mapping edits (including PROT_NONE reservations) advance the code
        // generation. Ordinary data stores do not. Fork/exec have a new space
        // identity; shared VM wrappers report their underlying owner's key.
        // Backing byte/EOF epochs do not change the displayed mapping ranges.
        let (space, generation, _) = p.memory.instruction_identity();
        let identity = (space, generation);
        let mut cache = self.proc_maps.borrow_mut();
        if let Some(maps) = cache.get(&space).filter(|maps| maps.identity == identity) {
            return maps.bytes.clone();
        }
        let mut maps = String::new();
        for (start, end, access) in p.memory.regions() {
            use std::fmt::Write;
            let _ = writeln!(
                maps,
                "{start:012x}-{end:012x} {}{}{}p 00000000 00:00 0",
                if access.read { 'r' } else { '-' },
                if access.write { 'w' } else { '-' },
                if access.execute { 'x' } else { '-' }
            );
            if maps.len() >= 1024 * 1024 {
                break;
            }
        }
        let maps = maps.into_bytes();
        cache.insert(
            space,
            ProcMaps {
                identity,
                bytes: maps.clone(),
            },
        );
        maps
    }
    fn process_info(&self, pid: u32, p: &Process) -> trames_vfs::ProcessInfo {
        let maps = self.process_maps(p);
        let arguments = p
            .linux
            .argument_pointers
            .iter()
            .enumerate()
            .map(|(i, pointer)| {
                p.memory
                    .read_c_string(
                        *pointer,
                        p.linux.arguments.get(i).map_or(65536, |a| a.len() + 1),
                    )
                    .unwrap_or_else(|_| p.linux.arguments.get(i).cloned().unwrap_or_default())
            })
            .collect();
        trames_vfs::ProcessInfo {
            pid,
            tgid: p.tgid,
            parent: p.parent,
            group: p.group,
            session: p.session,
            state: match p.state {
                State::Ready => 'R',
                State::Stopped => 'T',
                State::Zombie(_) => 'Z',
                _ => 'S',
            },
            credentials: p.linux.vfs.credentials.clone(),
            namespaces: p.linux.vfs.namespace_snapshot(),
            namespace_ids: [p.tgid, pid, p.group, p.session]
                .map(|id| p.linux.vfs.pid_namespace().hierarchy(id)),
            command: p.linux.command_name.clone(),
            cgroup: p.linux.vfs.cgroup_path(pid),
            nondumpable: p.linux.vfs.dumpable() != 1,
            seccomp: p.seccomp.mode(),
            seccomp_filters: p.seccomp.count(),
            arguments,
            environment: p.linux.environment.clone(),
            executable: p.linux.executable_file.as_ref().map_or_else(
                || p.linux.executable_path.clone(),
                |file| p.linux.vfs.executable_display(file),
            ),
            executable_file: p.linux.executable_file.as_ref().map(Rc::downgrade),
            cwd: p.linux.vfs.cwd().unwrap_or_else(|_| b"/".to_vec()),
            mapped_bytes: p.memory.mapped_bytes(),
            virtual_bytes: p.memory.virtual_bytes(),
            maps,
            affinity: p.affinity,
            processor: p.last_cpu as u32,
            nice: p.nice,
            scheduler: p.scheduler,
            user_ticks: (p.cpu_time.as_millis() / 10) as u64,
            start_ticks: (p
                .started
                .saturating_duration_since(self.started)
                .as_millis()
                / 10) as u64,
            terminal: p.linux.vfs.controlling_terminal.is_some(),
            terminal_number: p.linux.vfs.controlling_terminal.unwrap_or(0),
            fds: p.linux.vfs.fd_snapshot(),
            auxiliary: p.linux.auxiliary.clone(),
            file_size_limits: Some(p.file_size_limit.get()),
            oom: Rc::downgrade(&p.oom),
            pending: p.pending.bits(),
            shared_pending: p.shared_pending.borrow().bits(),
            blocked: p.mask,
            ignored: (1..=64)
                .filter(|s| p.actions[*s].handler == 1)
                .fold(0, |v, s| v | (1 << (s - 1))),
            caught: (1..=64)
                .filter(|s| p.actions[*s].handler > 1)
                .fold(0, |v, s| v | (1 << (s - 1))),
        }
    }
    fn refresh_proc(&self, pid: u32, p: &mut Process) {
        // Bound cached bytes by the current memory-owner set, even after repeated
        // fork/exec. Never cache fd paths, credentials, names, argv bytes or
        // other mutable process metadata under a mapping-generation key.
        let live: std::collections::BTreeSet<_> = self
            .processes
            .values()
            .map(|p| p.memory.instruction_identity().0)
            .chain(std::iter::once(p.memory.instruction_identity().0))
            .collect();
        self.proc_maps
            .borrow_mut()
            .retain(|vm, _| live.contains(vm));
        let mut processes: Vec<_> = self
            .processes
            .iter()
            .map(|(&id, other)| self.process_info(id, other))
            .collect();
        processes.push(self.process_info(pid, p));
        p.linux.vfs.set_runtime_info(
            pid,
            trames_vfs::RuntimeInfo {
                cpu_count: self.cpu_count,
                per_cpu_ticks: self
                    .per_cpu
                    .iter()
                    .map(|t| (t.as_millis() / 10) as u64)
                    .collect(),
                cpu_features: trames_cpu::FEATURE_NAMES,
                processes,
                uptime: self.started.elapsed(),
                boot_time: self.boot_time,
                context_switches: self.context_switches,
                cpu_user_ticks: (self.cpu_total.as_millis() / 10) as u64,
                idle: self.idle,
                memory_limit: self.memory_limits.total,
                load: self.load,
                processes_created: self.next_pid - 1,
                used_memory: self.other_memory(p.vm_id) + p.memory.mapped_bytes(),
            },
        );
    }
    fn notify(&mut self, parent: u32, child: u32, uid: u32, status: i32, code: i32) {
        if let Some(p) = self.processes.get_mut(&parent) {
            if p.actions[17].handler == 1 || (matches!(code, 5 | 6) && p.actions[17].flags & 1 != 0)
            {
                return;
            }
            let _ = p.shared_pending.borrow_mut().enqueue(
                17,
                Info {
                    sender: child,
                    uid,
                    status,
                    code,
                    timer: None,
                    fault: None,
                },
            );
        }
    }
    fn close_exit_files(&self, p: &mut Process) {
        p.linux.executable_file = None;
        if !self
            .processes
            .values()
            .any(|other| other.vm_id == p.vm_id && !matches!(other.state, State::Zombie(_)))
        {
            self.gpu.borrow_mut().remove(&p.vm_id);
        }
        p.linux.vfs.set_lock_owner(p.tgid);
        let live = |t: &&Process| !matches!(t.state, State::Zombie(_));
        if !self
            .processes
            .values()
            .filter(live)
            .any(|t| t.tgid == p.tgid)
        {
            p.linux.vfs.release_process_locks();
        }
        if self
            .processes
            .values()
            .filter(live)
            .any(|t| t.files_id == p.files_id)
        {
            p.linux.vfs.detach_file_table();
        } else {
            p.linux.vfs.close_all();
        }
    }
    fn exit_one(&mut self, pid: u32, p: &mut Process, status: i32) {
        self.parent_thread_exited(pid, p, None);
        p.linux.vfs.cgroup_exit(pid);
        let state = if matches!(p.state, State::Stopped) {
            p.stopped_state.as_deref()
        } else {
            Some(&p.state)
        };
        if let Some(State::Gpu { job, .. }) = state {
            if let Some(backend) = self.gpu.borrow_mut().get_mut(&p.vm_id) {
                backend.abandon(*job);
            }
        }
        let address = p.linux.clear_tid_address();
        p.linux.exit_tid(&mut p.memory);
        self.wake_futex(p, address, u32::MAX, u32::MAX);
        self.wake_vfork(pid);
        if pid != p.tgid {
            p.state = State::Zombie(status);
            p.auto_reap = true;
            self.close_exit_files(p);
            p.memory = Memory::default();
            self.finish_group(p.tgid);
            return;
        }
        p.auto_reap = self.processes.get(&p.parent).is_some_and(|parent| {
            parent.actions[17].handler == 1 || parent.actions[17].flags & 2 != 0
        });
        if pid != 1 {
            p.memory = Memory::default();
        }
        p.state = State::Zombie(status);
        self.close_exit_files(p);
        p.pending.clear();
        p.wait_event = None;
        if !self
            .processes
            .values()
            .any(|t| t.tgid == p.tgid && !matches!(t.state, State::Zombie(_)))
        {
            self.reap_namespace_children(pid, p.linux.vfs.pid_namespace(), p.parent);
            self.timers.retain(|_, t| t.tgid != p.tgid);
            self.notify(
                p.parent,
                pid,
                p.linux.vfs.credentials.uid,
                if status & 127 == 0 {
                    status >> 8
                } else {
                    status & 127
                },
                if status & 127 == 0 { 1 } else { 2 },
            );
        } else {
            p.auto_reap = false;
        }
    }
    fn finish_group(&mut self, tgid: u32) {
        if self
            .processes
            .values()
            .any(|t| t.tgid == tgid && !matches!(t.state, State::Zombie(_)))
        {
            return;
        }
        self.timers.retain(|_, t| t.tgid != tgid);
        if let Some(leader) = self.processes.get(&tgid) {
            if let State::Zombie(status) = leader.state {
                let parent = leader.parent;
                let uid = leader.linux.vfs.credentials.uid;
                let namespace = leader.linux.vfs.pid_namespace().clone();
                self.reap_namespace_children(tgid, &namespace, parent);
                self.notify(
                    parent,
                    tgid,
                    uid,
                    if status & 127 == 0 {
                        status >> 8
                    } else {
                        status & 127
                    },
                    if status & 127 == 0 { 1 } else { 2 },
                );
                let auto = self
                    .processes
                    .get(&parent)
                    .is_some_and(|p| p.actions[17].handler == 1 || p.actions[17].flags & 2 != 0);
                if auto && tgid != 1 {
                    self.processes.remove(&tgid);
                }
            }
        }
    }
    fn exit(&mut self, pid: u32, p: &mut Process, status: i32) {
        let members: Vec<_> = self
            .processes
            .iter()
            .filter(|(_, t)| t.tgid == p.tgid)
            .map(|(&id, _)| id)
            .collect();
        for id in members.iter().copied().filter(|id| *id != p.tgid) {
            if let Some(mut thread) = self.processes.remove(&id) {
                thread.linux.vfs.cgroup_exit(id);
                let address = thread.linux.clear_tid_address();
                thread.linux.exit_tid(&mut thread.memory);
                self.wake_futex(&thread, address, u32::MAX, u32::MAX);
                self.close_exit_files(&mut thread);
            }
        }
        if pid != p.tgid {
            if let Some(mut leader) = self.processes.remove(&p.tgid) {
                self.exit_one(p.tgid, &mut leader, status);
                self.processes.insert(p.tgid, leader);
            }
        }
        self.exit_one(pid, p, status);
    }
    fn wake_vfork(&mut self, child: u32) {
        for parent in self.processes.values_mut() {
            if matches!(parent.state, State::Vfork(id) if id == child) {
                parent.state = State::Ready;
            }
        }
    }
    fn prepare_signal(p: &mut Process, sig: u32, sender: u32) -> bool {
        if sig == 0 || matches!(p.state, State::Zombie(_)) {
            return false;
        }
        if sig == 18 {
            for s in 19..=22 {
                p.pending.discard(s);
                p.shared_pending.borrow_mut().discard(s);
            }
            if matches!(p.state, State::Stopped) {
                p.state = p.stopped_state.take().map_or(State::Ready, |s| *s);
                p.wait_event = Some(0xffff);
            }
        }
        if matches!(sig, 19..=22) {
            p.pending.discard(18);
            p.shared_pending.borrow_mut().discard(18);
        }
        // Unblocked default dispositions are ignored by namespace init. Blocked
        // signals remain available to signalfd/sigwait. Ancestor KILL/STOP and
        // synchronous faults retain their forced-delivery behavior.
        if p.linux.vfs.pid_namespace().is_nested()
            && p.linux.vfs.visible_pid(p.tgid) == 1
            && p.actions[sig as usize].handler <= 1
            && p.mask & (1 << (sig - 1)) == 0
            && !(matches!(sig, 9 | 19) && p.linux.vfs.visible_pid(sender) == 0)
        {
            return false;
        }
        if sig != 9
            && sig != 19
            && p.actions[sig as usize].handler == 1
            && p.mask & (1 << (sig - 1)) == 0
        {
            return false;
        }
        true
    }
    fn queue_one(_pid: u32, p: &mut Process, sig: u32, sender: u32) {
        if Self::prepare_signal(p, sig, sender) {
            let _ = p.pending.enqueue(
                sig,
                Info {
                    sender,
                    uid: 0,
                    status: 0,
                    code: 0,
                    timer: None,
                    fault: None,
                },
            );
        }
    }
    fn may_signal(source: &Process, target: &Process, sig: u32) -> bool {
        let a = &source.linux.vfs.credentials;
        let b = &target.linux.vfs.credentials;
        a.has_cap(capabilities::KILL)
            || [a.uid, a.euid]
                .iter()
                .any(|id| *id == b.uid || *id == b.suid)
            || sig == 18 && source.session == target.session
    }
    fn thread_signal(
        &mut self,
        pid: u32,
        p: &mut Process,
        tid: u32,
        tgid: Option<u32>,
        sig: u32,
    ) -> Result<u64, Errno> {
        if sig > 64 {
            return Err(Errno(22));
        }
        if tid == 0 || tid > i32::MAX as u32 {
            return Err(Errno(22));
        }
        let tid = p.linux.vfs.global_pid(tid)?;
        let tgid = tgid.map(|id| p.linux.vfs.global_pid(id)).transpose()?;
        let allowed = if tid == pid {
            if tgid.is_some_and(|id| id != p.tgid) {
                return Err(Errno(3));
            }
            Self::may_signal(p, p, sig)
        } else {
            let target = self.processes.get(&tid).ok_or(Errno(3))?;
            if tgid.is_some_and(|id| id != target.tgid) {
                return Err(Errno(3));
            }
            Self::may_signal(p, target, sig)
        };
        if !allowed {
            return Err(Errno(1));
        }
        let sender = p.tgid;
        let uid = p.linux.vfs.credentials.uid;
        if matches!(sig, 18..=22) {
            // Stop/continue cancellation and group resume apply to every member
            // even when tgkill selects one thread for the pending signal.
            let group = if tid == pid {
                p.tgid
            } else {
                self.processes[&tid].tgid
            };
            if p.tgid == group {
                Self::prepare_signal(p, sig, sender);
            }
            for other in self
                .processes
                .values_mut()
                .filter(|other| other.tgid == group)
            {
                Self::prepare_signal(other, sig, sender);
            }
        }
        let target = if tid == pid {
            p
        } else {
            self.processes.get_mut(&tid).ok_or(Errno(3))?
        };
        if Self::prepare_signal(target, sig, sender) {
            target.pending.enqueue(
                sig,
                Info {
                    sender,
                    uid,
                    status: 0,
                    code: -6,
                    timer: None,
                    fault: None,
                },
            )?;
        }
        Ok(0)
    }
    fn signal(&mut self, pid: u32, p: &mut Process, target: i32, sig: u32) -> Result<u64, Errno> {
        if sig > 64 {
            return Err(Errno(22));
        }
        let positive = if target > 0 {
            let target = p.linux.vfs.global_pid(target as u32)?;
            if target == pid {
                p.tgid
            } else {
                self.processes.get(&target).ok_or(Errno(3))?.tgid
            }
        } else {
            0
        };
        let matches = |id: u32, group: u32| {
            if p.linux.vfs.visible_pid(id) == 0 {
                return false;
            }
            if target > 0 {
                id == positive
            } else if target == 0 {
                group == p.group
            } else if target == -1 {
                p.linux.vfs.visible_pid(id) != 1 && id != p.tgid
            } else {
                p.linux.vfs.visible_pid(group) == target.unsigned_abs()
            }
        };
        // One process-directed pending signal per thread group; stop/continue
        // state changes must reach every member, not just the group leader.
        let mut groups = Vec::new();
        let mut found = false;
        if pid == p.tgid && matches(p.tgid, p.group) {
            found = true;
            if Self::may_signal(p, p, sig) {
                groups.push(p.tgid);
            }
        }
        for (&id, other) in &self.processes {
            if id == other.tgid && matches(other.tgid, other.group) {
                found = true;
                if Self::may_signal(p, other, sig) {
                    groups.push(other.tgid);
                }
            }
        }
        if groups.is_empty() {
            return Err(Errno(if found { 1 } else { 3 }));
        }
        let sender = p.tgid;
        let uid = p.linux.vfs.credentials.uid;
        let mut queued = Vec::new();
        let mut queue = |other: &mut Process| -> Result<(), Errno> {
            if groups.contains(&other.tgid)
                && Self::prepare_signal(other, sig, sender)
                && !queued.contains(&other.tgid)
            {
                other.shared_pending.borrow_mut().enqueue(
                    sig,
                    Info {
                        sender,
                        uid,
                        status: 0,
                        code: 0,
                        timer: None,
                        fault: None,
                    },
                )?;
                queued.push(other.tgid);
            }
            Ok(())
        };
        queue(p)?;
        for other in self.processes.values_mut() {
            queue(other)?;
        }
        Ok(0)
    }
    fn synchronous_fault(&mut self, pid: u32, p: &mut Process, fault: &trames_cpu::CpuFault) {
        p.linux.vfs.log(
            4,
            &format!(
                "pid={pid} tgid={} {fault} comm={:?}",
                p.tgid,
                String::from_utf8_lossy(&p.linux.command_name)
            ),
        );
        use trames_cpu::CpuFault as F;
        use trames_memory::MemoryError as M;
        let (sig, code, address) = match fault {
            F::Trap {
                pc, signal, code, ..
            } => (*signal, *code, *pc),
            F::Instruction { pc, .. } => (4, 1, *pc),
            F::Memory {
                error: M::Bus(address),
                ..
            } => (7, 2, *address),
            F::Memory {
                error: M::BackingIo(_),
                pc,
            } => (7, 3, *pc),
            F::Memory {
                error: M::Alignment(address),
                ..
            } => (7, 1, *address),
            F::Memory {
                error: M::Permission { address, .. },
                ..
            } => (11, 2, *address),
            F::Memory {
                error: M::Unmapped(address) | M::AddressRange(address),
                ..
            } => (11, 1, *address),
            F::Memory { pc, .. } => (11, 1, *pc),
        };
        // Forced synchronous faults cannot be ignored or left masked forever.
        if p.actions[sig as usize].handler <= 1 || p.mask & (1 << (sig - 1)) != 0 {
            if pid == 1 {
                eprintln!("trames: {fault}");
            }
            self.exit(pid, p, sig as i32);
            return;
        }
        // A CPU fault must retain its precise address/context even if an
        // asynchronous signal of the same number was already pending.
        p.pending.discard(sig);
        let _ = p.pending.enqueue(
            sig,
            Info {
                sender: 0,
                uid: 0,
                status: 0,
                code,
                timer: None,
                fault: Some(address & 0x00ffffffffffffff),
            },
        );
        if self.deliver(pid, p).is_err() {
            self.exit(pid, p, 11);
        }
    }
    fn deliver(&mut self, pid: u32, p: &mut Process) -> Result<bool, Errno> {
        let stopped = matches!(p.state, State::Stopped);
        let selected = if p.pending_bits() & (1 << 8) != 0 {
            p.next_signal(1 << 8)
        } else if let Some((&sig, _)) = p.pending.iter().find(|(_, info)| info.fault.is_some()) {
            Some((sig, false))
        } else {
            let mut allowed = !p.mask | UNBLOCKABLE;
            if stopped {
                allowed &= 1 << 17;
            }
            if matches!(p.state, State::Gpu { .. }) {
                for (sig, action) in p.actions.iter().enumerate().skip(1) {
                    if action.handler > 1 {
                        allowed &= !(1 << (sig - 1));
                    }
                }
            }
            p.next_signal(allowed)
        };
        let Some((sig, shared)) = selected else {
            return Ok(false);
        };
        if stopped && sig != 9 && sig != 18 {
            return Ok(false);
        }
        let info = p.take_signal(sig, shared);
        let action = p.actions[sig as usize];
        // Timer-generated signals also reach this queue without queue_one.
        // Namespace init ignores default dispositions, except forced ancestor
        // KILL/STOP and synchronous faults, which must still make progress.
        if p.linux.vfs.pid_namespace().is_nested()
            && p.linux.vfs.visible_pid(p.tgid) == 1
            && action.handler == 0
            && info.fault.is_none()
            && !(matches!(sig, 9 | 19) && info.timer.is_none())
        {
            return Ok(true);
        }
        if sig == 9 || (action.handler == 0 && !matches!(sig, 17 | 18 | 23 | 28 | 19..=22)) {
            self.exit(pid, p, sig as i32);
            return Ok(true);
        }
        if sig == 19 || (action.handler == 0 && matches!(sig, 20..=22)) {
            if !matches!(p.state, State::Stopped) {
                p.stopped_state = Some(Box::new(p.state.clone()));
            }
            p.state = State::Stopped;
            for other in self.processes.values_mut().filter(|other| {
                other.tgid == p.tgid && !matches!(other.state, State::Zombie(_) | State::Stopped)
            }) {
                other.stopped_state = Some(Box::new(other.state.clone()));
                other.state = State::Stopped;
            }
            p.wait_event = Some(((sig as i32) << 8) | 0x7f);
            self.notify(p.parent, p.tgid, p.linux.vfs.credentials.uid, sig as i32, 5);
            return Ok(true);
        }
        if action.handler <= 1 {
            return Ok(true);
        }
        if p.frames >= 8 {
            self.exit(pid, p, 11);
            return Ok(true);
        }
        match p.state.clone() {
            State::Blocked(number, a) => {
                if let Some(fd) = p.pending_open.take() {
                    p.linux.vfs.cancel_fifo_open(fd);
                }
                if let Some(mask) = p.poll_mask.take() {
                    p.mask = mask;
                }
                p.poll_deadline = None;
                let timed_socket = p.socket_deadline.take().is_some();
                if !timed_socket
                    && action.flags & 0x10000000 != 0
                    && matches!(
                        number,
                        25 | 32
                            | 56
                            | 63
                            | 64
                            | 65
                            | 66
                            | 95
                            | 260
                            | 202
                            | 242
                            | 206
                            | 207
                            | 211
                            | 212
                    )
                {
                    put_args(&mut p.cpu, a);
                    p.cpu.pc = p.cpu.pc.wrapping_sub(4);
                } else {
                    p.cpu.set_x(0, error(Errno(4)));
                }
            }
            State::Sleep(deadline, remaining) => {
                if remaining != 0 {
                    let ns = deadline
                        .saturating_duration_since(Instant::now())
                        .as_nanos();
                    let _ = write_u64(&mut p.memory, remaining, (ns / 1_000_000_000) as u64);
                    let _ = write_u64(&mut p.memory, remaining + 8, (ns % 1_000_000_000) as u64);
                }
                p.cpu.set_x(0, error(Errno(4)));
            }
            State::Suspend(old) => {
                p.mask = old;
                p.cpu.set_x(0, error(Errno(4)));
            }
            State::Futex { .. } | State::Sigwait { .. } => {
                p.cpu.set_x(0, error(Errno(4)));
            }
            _ => {}
        }
        let old_alt = p.alt_stack;
        let use_alt =
            action.flags & 0x08000000 != 0 && old_alt.flags & 2 == 0 && !old_alt.on_stack(p.cpu.sp);
        let top = if use_alt {
            old_alt.sp.checked_add(old_alt.size).ok_or(Errno(14))?
        } else {
            p.cpu.sp
        };
        let sp = top.checked_sub(4688).ok_or(Errno(14))? & !15;
        if (use_alt || old_alt.contains(p.cpu.sp)) && sp < old_alt.sp {
            return Err(Errno(14));
        }
        Linux::check(&p.memory, sp, 4688, Access::Write)?;
        let mut frame = vec![0; 4688];
        frame[0..4].copy_from_slice(&sig.to_le_bytes());
        frame[8..12].copy_from_slice(&info.code.to_le_bytes());
        events::write_siginfo(&mut frame[..128], sig, info);
        frame[144..152].copy_from_slice(&old_alt.sp.to_le_bytes());
        frame[152..156].copy_from_slice(&old_alt.reported_flags(p.cpu.sp).to_le_bytes());
        frame[160..168].copy_from_slice(&old_alt.size.to_le_bytes());
        frame[168..176].copy_from_slice(&p.mask.to_le_bytes());
        frame[304..312].copy_from_slice(&info.fault.unwrap_or(0).to_le_bytes());
        for r in 0..31 {
            frame[312 + r * 8..320 + r * 8].copy_from_slice(&p.cpu.x(r).to_le_bytes());
        }
        frame[560..568].copy_from_slice(&p.cpu.sp.to_le_bytes());
        frame[568..576].copy_from_slice(&p.cpu.pc.to_le_bytes());
        frame[576..584].copy_from_slice(&((p.cpu.nzcv() as u64) << 28).to_le_bytes());
        frame[592..596].copy_from_slice(&0x46508001u32.to_le_bytes());
        frame[596..600].copy_from_slice(&528u32.to_le_bytes());
        frame[600..604].copy_from_slice(&p.cpu.fpsr.to_le_bytes());
        frame[604..608].copy_from_slice(&p.cpu.fpcr.to_le_bytes());
        for r in 0..32 {
            frame[608 + r * 16..624 + r * 16].copy_from_slice(&p.cpu.vector(r).to_le_bytes());
        }
        p.memory.write(sp, &frame).map_err(|_| Errno(14))?;
        if old_alt.flags & 0x80000000 != 0 {
            p.alt_stack = AltStack::default();
        }
        p.mask |= action.mask;
        if action.flags & 0x40000000 == 0 {
            p.mask |= 1u64 << (sig - 1);
        }
        p.mask &= !UNBLOCKABLE;
        if action.flags & 0x80000000 != 0 {
            p.actions[sig as usize] = Action::default();
        }
        p.cpu.set_x(0, sig as u64);
        p.cpu.set_x(1, sp);
        p.cpu.set_x(2, sp + 128);
        p.cpu.set_x(
            30,
            if action.flags & 0x04000000 == 0 {
                0x7fff_ffd0_0000
            } else {
                action.restorer
            },
        );
        p.cpu.sp = sp;
        p.cpu.pc = action.handler;
        p.frames += 1;
        p.state = State::Ready;
        Ok(true)
    }
    fn sigreturn(p: &mut Process) -> Result<(), Errno> {
        if p.frames == 0 {
            return Err(Errno(14));
        }
        let sp = p.cpu.sp;
        Linux::check(&p.memory, sp, 4688, Access::Read)?;
        let mut frame = vec![0; 4688];
        p.memory.read(sp, &mut frame).map_err(|_| Errno(14))?;
        let u64_at = |i| u64::from_le_bytes(frame[i..i + 8].try_into().expect("frame"));
        let new_sp = u64_at(560);
        let new_pc = u64_at(568);
        if new_sp & 15 != 0 || new_sp >= (1 << 48) || new_pc & 3 != 0 || new_pc >= (1 << 48) {
            return Err(Errno(14));
        }
        if read_u32(&p.memory, sp + 592)? != 0x46508001 || read_u32(&p.memory, sp + 596)? != 528 {
            return Err(Errno(14));
        }
        let restored_alt = AltStack::restore(&p.memory, sp + 144)?;
        for r in 0..31 {
            p.cpu.set_x(r, u64_at(312 + r * 8));
        }
        p.cpu.sp = new_sp;
        p.cpu.pc = new_pc;
        p.cpu.set_nzcv((u64_at(576) >> 28) as u32);
        p.cpu.fpsr = read_u32(&p.memory, sp + 600)? & 0x0800009f;
        p.cpu.fpcr = read_u32(&p.memory, sp + 604)? & 0x03c00000;
        for r in 0..32 {
            p.cpu.set_vector(
                r,
                u128::from_le_bytes(
                    frame[608 + r * 16..624 + r * 16]
                        .try_into()
                        .expect("vector"),
                ),
            );
        }
        p.mask = u64_at(168) & !UNBLOCKABLE;
        p.alt_stack = restored_alt;
        p.frames -= 1;
        p.state = State::Ready;
        Ok(())
    }
    fn exec(p: &mut Process, a: [u64; 6], budget: usize) -> Result<(), Errno> {
        Self::exec_at(p, AT_FDCWD, a, 0, budget)
    }
    fn exec_at(
        p: &mut Process,
        dirfd: i32,
        a: [u64; 6],
        flags: u32,
        budget: usize,
    ) -> Result<(), Errno> {
        let mut path = Linux::path(&p.memory, a[0])?;
        if path.is_empty() && flags & 0x1000 == 0 {
            return Err(Errno(2));
        }
        if flags & !0x1100 != 0 {
            return Err(Errno(22));
        }
        let via_fd = path.is_empty() || !path.starts_with(b"/") && dirfd != AT_FDCWD;
        let filename = if via_fd {
            let mut name = format!("/dev/fd/{dirfd}").into_bytes();
            if !path.is_empty() {
                name.push(b'/');
                name.extend_from_slice(&path);
            }
            name
        } else {
            path.clone()
        };
        let mut file = p.linux.vfs.executable_file_at(dirfd, &path, flags)?;
        let inaccessible = via_fd && p.linux.vfs.flags(dirfd as u32)? & trames_vfs::O_CLOEXEC != 0;
        let mut argv = strings(&p.memory, a[1])?;
        let envp = strings(&p.memory, a[2])?;
        if argv.is_empty() {
            argv.push(Vec::new());
        }
        for depth in 0..5 {
            let bytes = file.bytes()?;
            if bytes.starts_with(b"#!") {
                if depth == 0 && inaccessible {
                    return Err(Errno(2));
                }
                if depth == 4 {
                    return Err(Errno(40));
                }
                let line = bytes[2..].split(|&b| b == b'\n').next().ok_or(Errno(8))?;
                if line.len() > 254 {
                    return Err(Errno(8));
                }
                let line = line.strip_suffix(b"\r").unwrap_or(line);
                let start = line
                    .iter()
                    .position(|b| !b.is_ascii_whitespace())
                    .ok_or(Errno(8))?;
                let line = &line[start..];
                let mut parts = line.splitn(2, |b| b.is_ascii_whitespace());
                let interpreter = parts.next().unwrap_or_default();
                if interpreter.is_empty() {
                    return Err(Errno(8));
                }
                let mut next = vec![interpreter.to_vec()];
                if let Some(optional) = parts.next() {
                    let trimmed = optional
                        .iter()
                        .position(|b| !b.is_ascii_whitespace())
                        .map(|i| &optional[i..])
                        .unwrap_or_default();
                    if !trimmed.is_empty() {
                        next.push(trimmed.to_vec());
                    }
                }
                next.push(if depth == 0 {
                    filename.clone()
                } else {
                    path.clone()
                });
                next.extend(argv.into_iter().skip(1));
                argv = next;
                path = interpreter.to_vec();
                file = p.linux.vfs.executable_file_at(AT_FDCWD, &path, 0)?;
                continue;
            }
            let mut execute_vfs = p.linux.vfs.clone();
            execute_vfs.apply_exec_file_credentials(&file)?;
            let mut runtime = Runtime::load_guest_execfn(
                &bytes,
                &argv,
                &envp,
                &execute_vfs,
                super::RuntimeOptions {
                    memory_limits: super::MemoryLimits {
                        per_process: budget,
                        total: budget,
                    },
                    pure_accelerator: accelerated(
                        p,
                        trames_accel::native::AUXV_KEY,
                        trames_accel::native::PROFILE,
                    ),
                    vulkan_accelerator: accelerated(
                        p,
                        trames_vulkan::AUXV_KEY,
                        trames_vulkan::PROFILE,
                    ),
                    native_bsearch: p.linux.auxiliary.chunks_exact(16).any(|pair| {
                        pair[..8] == trames_accel::callbacks::SEARCH_AUXV_KEY.to_le_bytes()
                            && pair[8..] == 1u64.to_le_bytes()
                    }),
                    native_allocator: p.linux.auxiliary.chunks_exact(16).any(|pair| {
                        pair[..8] == trames_accel::allocator::AUXV_KEY.to_le_bytes()
                            && pair[8..] == trames_accel::allocator::PROFILE.to_le_bytes()
                    }),
                },
                Some(&filename),
            )
            .map_err(|e| match e {
                RuntimeError::Arguments(_) => Errno(7),
                RuntimeError::Memory(_) | RuntimeError::Configuration(_) => Errno(12),
                RuntimeError::Elf(trames_elf::ElfError::Interpreter(n)) => Errno(n),
                _ => Errno(8),
            })?;
            if runtime.memory.mapped_bytes() > budget {
                return Err(Errno(12));
            }
            runtime.linux.executable_path = p.linux.vfs.executable_display(&file);
            runtime.linux.executable_file = Some(file);
            runtime.linux.command_name = filename
                .rsplit(|b| *b == b'/')
                .next()
                .unwrap_or_default()
                .iter()
                .copied()
                .take(15)
                .collect();
            p.linux.vfs.close_exec();
            let credentials = execute_vfs.credentials.clone();
            std::mem::swap(&mut runtime.linux.vfs, &mut p.linux.vfs);
            runtime.linux.vfs.credentials = credentials;
            runtime.linux.vfs.unshare_dumpable();
            runtime.linux.vfs.set_dumpable(execute_vfs.dumpable())?;
            runtime.linux.trace = p.linux.trace;
            p.linux = runtime.linux;
            p.cpu = runtime.cpu;
            p.memory = runtime.memory;
            for action in &mut p.actions {
                if action.handler != 1 {
                    *action = Action::default();
                }
            }
            p.frames = 0;
            p.alt_stack = AltStack::default();
            p.membarrier_registered = Rc::new(Cell::new(false));
            p.state = State::Ready;
            return Ok(());
        }
        Err(Errno(40))
    }
    fn wait(&mut self, _pid: u32, p: &mut Process, a: [u64; 6]) -> Result<Option<u64>, Errno> {
        // __WALL includes all waitable clone children. Non-thread guest clones
        // currently all use SIGCHLD, so this preserves the ordinary selection.
        if a[2] & !(11 | 0x40000000) != 0 {
            return Err(Errno(22));
        }
        if a[1] != 0 {
            Linux::check(&p.memory, a[1], 4, Access::Write)?;
        }
        if a[3] != 0 {
            Linux::check(&p.memory, a[3], 144, Access::Write)?;
        }
        let target = a[0] as i32;
        let matching: Vec<u32> = self
            .processes
            .iter()
            .filter(|(id, c)| {
                c.parent == p.tgid
                    && **id == c.tgid
                    && (target == -1
                        || (target == 0 && c.group == p.group)
                        || (target < -1
                            && p.linux.vfs.visible_pid(c.group) == target.unsigned_abs())
                        || (target > 0 && p.linux.vfs.visible_pid(**id) == target as u32))
            })
            .map(|(&id, _)| id)
            .collect();
        if matching.is_empty() {
            return Err(Errno(10));
        }
        for child in matching {
            let c = &self.processes[&child];
            let status = match c.state {
                State::Zombie(s)
                    if !self
                        .processes
                        .values()
                        .any(|t| t.tgid == c.tgid && !matches!(t.state, State::Zombie(_))) =>
                {
                    Some(s)
                }
                State::Stopped if a[2] & 2 != 0 => c.wait_event,
                _ if a[2] & 8 != 0 && c.wait_event == Some(0xffff) => Some(0xffff),
                _ => None,
            };
            if let Some(status) = status {
                if a[1] != 0 {
                    p.memory
                        .write(a[1], &status.to_le_bytes())
                        .map_err(|_| Errno(14))?;
                }
                if a[3] != 0 {
                    p.memory.write(a[3], &[0; 144]).map_err(|_| Errno(14))?;
                }
                if matches!(c.state, State::Zombie(_)) {
                    self.processes.remove(&child);
                } else {
                    self.processes.get_mut(&child).expect("child").wait_event = None;
                }
                return Ok(Some(p.linux.vfs.visible_pid(child) as u64));
            }
        }
        if a[2] & 1 != 0 {
            Ok(Some(0))
        } else {
            Ok(None)
        }
    }
    fn syscall(
        &mut self,
        pid: u32,
        p: &mut Process,
        number: u32,
        a: [u64; 6],
        io: &mut impl GuestOutput,
    ) -> Result<Option<u64>, Errno> {
        match number {
            277 => self.seccomp(p, a).map(Some),
            167 if a[0] == 21 => Ok(Some(p.seccomp.mode() as u64)),
            167 if a[0] == 22 => match a[1] {
                // PR_SET_SECCOMP uses mode 1/2, while seccomp(2) uses
                // operation 0/1. Keep validation and actual filters shared.
                1 => self.seccomp(p, [0, 0, 0, 0, 0, 0]).map(Some),
                2 => self.seccomp(p, [1, 0, a[2], 0, 0, 0]).map(Some),
                _ => Err(Errno(22)),
            },
            140 | 141 => self.priority(pid, p, number, a),
            236 => Self::mempolicy(p, a),
            90 | 91 => self.capcall(pid, p, number == 91, a).map(Some),
            93 | 94 => {
                if number == 93 {
                    self.exit_one(pid, p, (a[0] as u8 as i32) << 8);
                } else {
                    self.exit(pid, p, (a[0] as u8 as i32) << 8);
                }
                Ok(None)
            }
            118..=123 => {
                if (a[0] as u32 as i32) < 0 {
                    return Err(Errno(22));
                }
                let target_id = if a[0] as u32 == 0 {
                    pid
                } else {
                    p.linux.vfs.global_pid(a[0] as u32)?
                };
                let target = if target_id == pid {
                    &*p
                } else {
                    self.processes
                        .get(&target_id)
                        .filter(|p| !matches!(p.state, State::Zombie(_)))
                        .ok_or(Errno(3))?
                };
                if matches!(number, 118 | 119 | 122)
                    && !p.linux.vfs.credentials.has_cap(capabilities::SYS_NICE)
                    && ((p.linux.vfs.credentials.euid != target.linux.vfs.credentials.uid
                        && p.linux.vfs.credentials.euid != target.linux.vfs.credentials.euid)
                        || target.linux.vfs.credentials.caps.permitted
                            & !p.linux.vfs.credentials.caps.permitted
                            != 0)
                {
                    return Err(Errno(1));
                }
                match number {
                    118 | 119 => {
                        let pointer = if number == 118 { a[1] } else { a[2] };
                        let priority = read_u32(&p.memory, pointer)? as i32;
                        if number == 119 && !matches!(a[1] as u32, 0 | 3 | 5) {
                            return Err(Errno(if matches!(a[1] as u32, 1 | 2) { 95 } else { 22 }));
                        }
                        if priority != 0 {
                            return Err(Errno(22));
                        }
                        if number == 119 {
                            if target_id == pid {
                                p.scheduler = a[1] as u32;
                            } else {
                                self.processes.get_mut(&target_id).unwrap().scheduler = a[1] as u32;
                            }
                        }
                        Ok(Some(0)) // Policy is guest metadata; host workers retain their priorities.
                    }
                    120 => Ok(Some(target.scheduler as u64)),
                    121 => {
                        Linux::check(&p.memory, a[1], 4, Access::Write)?;
                        p.memory.write(a[1], &[0; 4]).map_err(|_| Errno(14))?;
                        Ok(Some(0))
                    }
                    122 => {
                        if a[1] == 0 {
                            return Err(Errno(22));
                        }
                        Linux::check(&p.memory, a[2], a[1].min(8) as usize, Access::Read)?;
                        let mut bytes = [0; 8];
                        p.memory
                            .read(a[2], &mut bytes[..a[1].min(8) as usize])
                            .map_err(|_| Errno(14))?;
                        let mask = u64::from_le_bytes(bytes) & ((1u64 << self.cpu_count) - 1);
                        if mask == 0 {
                            return Err(Errno(22));
                        }
                        if target_id == pid {
                            p.affinity = mask;
                        } else {
                            self.processes.get_mut(&target_id).ok_or(Errno(3))?.affinity = mask;
                        }
                        Ok(Some(0))
                    }
                    _ => {
                        if a[1] < 8 {
                            return Err(Errno(22));
                        }
                        let mask = target.affinity;
                        write_u64(&mut p.memory, a[2], mask)?;
                        Ok(Some(8))
                    }
                }
            }
            39 => {
                p.linux.vfs.umount(&Linux::path(&p.memory, a[0])?, a[1])?;
                Ok(Some(0))
            }
            40 => {
                let optional = |ptr| {
                    if ptr == 0 {
                        Ok(None)
                    } else {
                        Linux::path(&p.memory, ptr).map(Some)
                    }
                };
                let source = optional(a[0])?;
                let target = Linux::path(&p.memory, a[1])?;
                let kind = optional(a[2])?;
                let data = optional(a[4])?;
                p.linux.vfs.mount(
                    source.as_deref(),
                    &target,
                    kind.as_deref(),
                    a[3],
                    data.as_deref(),
                )?;
                Ok(Some(0))
            }
            41 => {
                let before = p.linux.vfs.clone();
                p.linux.vfs.pivot_root(
                    &Linux::path(&p.memory, a[0])?,
                    &Linux::path(&p.memory, a[1])?,
                )?;
                for other in self.processes.values_mut() {
                    other.linux.vfs.sync_pivot_from(&before, &p.linux.vfs);
                }
                Ok(Some(0))
            }
            97 => {
                const ALLOWED: u64 = trames_vfs::CLONE_NEWUTS
                    | trames_vfs::CLONE_NEWNS
                    | trames_vfs::CLONE_NEWPID
                    | trames_vfs::CLONE_NEWNET
                    | trames_vfs::CLONE_NEWIPC
                    | trames_vfs::CLONE_NEWCGROUP
                    | 0x200
                    | 0x400;
                if a[0] & !ALLOWED != 0 {
                    return Err(Errno(22));
                }
                if a[0] & trames_vfs::CLONE_NEWPID != 0
                    && (pid != p.tgid
                        || self
                            .processes
                            .values()
                            .any(|t| t.tgid == p.tgid && !matches!(t.state, State::Zombie(_))))
                {
                    return Err(Errno(22));
                }
                let mut view = p.linux.vfs.clone();
                if a[0] & trames_vfs::CLONE_NEWPID != 0 {
                    view.unshare_pid()?;
                }
                if a[0] & trames_vfs::CLONE_NEWUTS != 0 {
                    view.unshare_uts()?;
                }
                if a[0] & trames_vfs::CLONE_NEWNS != 0 {
                    view.unshare_mounts()?;
                }
                if a[0] & trames_vfs::CLONE_NEWNET != 0 {
                    view.unshare_net()?;
                }
                if a[0] & trames_vfs::CLONE_NEWIPC != 0 {
                    view.unshare_ipc()?;
                }
                if a[0] & trames_vfs::CLONE_NEWCGROUP != 0 {
                    view.unshare_cgroup()?;
                }
                p.linux.vfs = view;
                if a[0] & (0x200 | trames_vfs::CLONE_NEWNS) != 0 {
                    p.fs_id = self.resource_id();
                }
                if a[0] & 0x400 != 0 {
                    p.files_id = self.resource_id();
                }
                Ok(Some(0))
            }
            268 => {
                if p.linux.vfs.namespace_type(a[0] as u32)? == Some(trames_vfs::CLONE_NEWNS)
                    && self.processes.values().any(|other| {
                        other.fs_id == p.fs_id && !matches!(other.state, State::Zombie(_))
                    })
                {
                    return Err(Errno(22));
                }
                p.linux.vfs.setns(a[0] as u32, a[1])?;
                Ok(Some(0))
            }
            436 => {
                let (first, last, flags) = (a[0] as u32, a[1] as u32, a[2] as u32);
                if first > last || flags & !6 != 0 {
                    return Err(Errno(22));
                }
                if flags & 2 != 0 {
                    p.files_id = self.resource_id();
                }
                p.linux.vfs.close_range(first, last, flags)?;
                Ok(Some(0))
            }
            168 => {
                if a[0] != 0 {
                    Linux::check(&p.memory, a[0], 4, Access::Write)?;
                }
                if a[1] != 0 {
                    Linux::check(&p.memory, a[1], 4, Access::Write)?;
                }
                if a[0] != 0 {
                    p.memory
                        .write(a[0], &(p.last_cpu as u32).to_le_bytes())
                        .map_err(|_| Errno(14))?;
                }
                if a[1] != 0 {
                    p.memory
                        .write(a[1], &0u32.to_le_bytes())
                        .map_err(|_| Errno(14))?;
                }
                Ok(Some(0))
            }
            172 => Ok(Some(p.linux.vfs.visible_pid(p.tgid) as u64)),
            178 => Ok(Some(p.linux.vfs.visible_pid(pid) as u64)),
            173 => Ok(Some(p.linux.vfs.visible_pid(p.parent) as u64)),
            96 => {
                p.linux.dispatch(&mut p.cpu, &mut p.memory, io);
                Ok(Some(p.linux.vfs.visible_pid(pid) as u64))
            }
            220 => {
                const ALLOWED: u64 = 0xff
                    | 0xf00
                    | 0x4000
                    | 0x8000
                    | 0x10000
                    | 0xfc0000
                    | 0x1000000
                    | trames_vfs::CLONE_NEWUTS
                    | trames_vfs::CLONE_NEWNS
                    | trames_vfs::CLONE_NEWPID
                    | trames_vfs::CLONE_NEWNET
                    | trames_vfs::CLONE_NEWIPC
                    | trames_vfs::CLONE_NEWCGROUP;
                let threaded = a[0] & 0x10000 != 0;
                let shared_vm = a[0] & 0x100 != 0;
                if a[0] & !ALLOWED != 0
                    || (a[0] & trames_vfs::CLONE_NEWPID != 0 && a[0] & (0x10000 | 0x8000) != 0)
                    || (threaded
                        && p.linux.vfs.pid_namespace().id()
                            != p.linux.vfs.pid_children_namespace().id())
                    || (a[0] & 0x8000 != 0 && p.linux.vfs.visible_pid(p.tgid) == 1)
                    || (a[0] & trames_vfs::CLONE_NEWNS != 0 && a[0] & 0x200 != 0)
                    || (a[0] & trames_vfs::CLONE_NEWIPC != 0 && a[0] & 0x40000 != 0)
                    || (!threaded && a[0] & 255 != 17)
                    || (threaded && (a[0] & 0x900 != 0x900 || a[0] & 255 != 0))
                    || (a[0] & 0x800 != 0 && !shared_vm)
                {
                    return Err(Errno(22));
                }
                if self.processes.len() + 1 >= MAX_PROCESSES
                    || self.other_memory(p.vm_id)
                        + p.memory.mapped_bytes() * if shared_vm { 1 } else { 2 }
                        > self.memory_limits.total
                {
                    return Err(Errno(11));
                }
                if a[0] & 0x100000 != 0 {
                    Linux::check(&p.memory, a[2], 4, Access::Write)?;
                }
                if a[0] & (0x1000000 | 0x200000) != 0 {
                    Linux::check(&p.memory, a[4], 4, Access::Write)?;
                }
                let id = self.next_pid;
                p.linux.vfs.cgroup_can_fork(pid)?;
                self.next_pid = self
                    .next_pid
                    .checked_add(1)
                    .filter(|v| *v < i32::MAX as u32)
                    .ok_or(Errno(11))?;
                let live = self
                    .processes
                    .iter()
                    .flat_map(|(&id, t)| {
                        [id, t.tgid, t.group, t.session]
                            .into_iter()
                            .chain(t.signal_senders())
                    })
                    .chain([pid, p.tgid, p.group, p.session])
                    .chain(p.signal_senders())
                    .collect();
                p.linux.vfs.pid_namespace().retain_ids(&live);
                let mut child = p.fork();
                if a[0] & trames_vfs::CLONE_NEWPID != 0 {
                    child.linux.vfs.unshare_pid()?;
                }
                if a[0] & trames_vfs::CLONE_NEWUTS != 0 {
                    child.linux.vfs.unshare_uts()?;
                }
                if a[0] & trames_vfs::CLONE_NEWNS != 0 {
                    child.linux.vfs.unshare_mounts()?;
                }
                if a[0] & trames_vfs::CLONE_NEWNET != 0 {
                    child.linux.vfs.unshare_net()?;
                }
                if a[0] & trames_vfs::CLONE_NEWIPC != 0 {
                    child.linux.vfs.unshare_ipc()?;
                }
                if a[0] & trames_vfs::CLONE_NEWCGROUP != 0 {
                    child.linux.vfs.unshare_cgroup()?;
                }
                child.linux.vfs.activate_child_pid(id)?;
                let parent_tid = p.linux.vfs.visible_pid(id);
                let child_tid = child.linux.vfs.visible_pid(id);
                // PID namespace init inherits its parent's PGID/SID/tty, as
                // Linux copy_process does. Ancestor-only IDs appear as zero
                // inside the new namespace; init can then establish setsid().
                let resource = self.resource_id();
                if shared_vm {
                    child.memory = p.memory.share();
                    child.membarrier_registered = p.membarrier_registered.clone();
                    if a[0] & 0x4000 == 0 {
                        child.alt_stack = AltStack::default();
                    }
                } else {
                    child.vm_id = resource;
                    child.linux.vfs.unshare_dumpable();
                }
                if a[0] & 0x400 == 0 {
                    child.files_id = resource;
                }
                if a[0] & 0x200 == 0 {
                    child.fs_id = resource;
                }
                if a[0] & 0x800 == 0 {
                    child.actions_id = resource;
                }
                child.tgid = if threaded { p.tgid } else { id };
                if threaded {
                    child.oom = Rc::new(p.oom.thread());
                    child.shared_pending = p.shared_pending.clone();
                    child.subreaper = p.subreaper;
                    child.file_size_limit = p.file_size_limit.clone();
                }
                child
                    .linux
                    .fork_tid(if a[0] & 0x200000 != 0 { a[4] } else { 0 });
                child.parent = if threaded || a[0] & 0x8000 != 0 {
                    p.parent
                } else {
                    p.tgid
                };
                child.parent_thread = if threaded || a[0] & 0x8000 != 0 {
                    p.parent_thread
                } else {
                    pid
                };
                child.cpu.set_x(0, 0);
                if a[1] != 0 {
                    child.cpu.sp = a[1];
                }
                if a[0] & 0x80000 != 0 {
                    child.cpu.tpidr_el0 = a[3];
                }
                if a[0] & 0x100000 != 0 {
                    p.memory
                        .write(a[2], &parent_tid.to_le_bytes())
                        .map_err(|_| Errno(14))?;
                }
                if a[0] & 0x1000000 != 0 {
                    child
                        .memory
                        .write(a[4], &child_tid.to_le_bytes())
                        .map_err(|_| Errno(14))?;
                }
                child.linux.vfs.cgroup_fork(pid, id, child.tgid);
                self.processes.insert(id, child);
                if a[0] & 0x4000 != 0 {
                    p.state = State::Vfork(id);
                    p.cpu.set_x(0, parent_tid as u64);
                    return Ok(None);
                }
                Ok(Some(parent_tid as u64))
            }
            221 | 281 => {
                let old_vm = p.vm_id;
                let others = self.other_memory(p.vm_id);
                if number == 221 {
                    Self::exec(
                        p,
                        a,
                        self.memory_limits
                            .per_process
                            .min(self.memory_limits.total.saturating_sub(others)),
                    )?;
                } else {
                    Self::exec_at(
                        p,
                        a[0] as i32,
                        [a[1], a[2], a[3], 0, 0, 0],
                        a[4] as u32,
                        self.memory_limits
                            .per_process
                            .min(self.memory_limits.total.saturating_sub(others)),
                    )?;
                }
                let ids: Vec<_> = self
                    .processes
                    .iter()
                    .filter(|(_, t)| t.tgid == p.tgid)
                    .map(|(&id, _)| id)
                    .collect();
                for id in ids {
                    if let Some(mut t) = self.processes.remove(&id) {
                        self.parent_thread_exited(id, &t, Some(p.tgid));
                        t.linux.vfs.cgroup_exit(id);
                        let address = t.linux.clear_tid_address();
                        t.linux.exit_tid(&mut t.memory);
                        self.wake_futex(&t, address, u32::MAX, u32::MAX);
                        if t.files_id == p.files_id {
                            t.linux.vfs.detach_file_table();
                        } else {
                            t.linux.vfs.close_all();
                        }
                    }
                }
                self.timers.retain(|_, t| t.tgid != p.tgid);
                self.wake_vfork(pid);
                if pid != p.tgid {
                    p.linux.vfs.cgroup_exec_rekey(pid, p.tgid);
                    p.rekey = Some(p.tgid);
                    for child in self.processes.values_mut() {
                        if child.parent_thread == pid {
                            child.parent_thread = p.tgid;
                        }
                    }
                }
                let resource = self.resource_id();
                p.vm_id = resource;
                p.files_id = resource;
                p.fs_id = resource;
                p.actions_id = resource;
                if !self
                    .processes
                    .values()
                    .any(|other| other.vm_id == old_vm && !matches!(other.state, State::Zombie(_)))
                {
                    self.gpu.borrow_mut().remove(&old_vm);
                }
                Ok(None)
            }
            98 | 102 | 103 | 107..=111 | 137 => self.event_call(pid, p, number, a),
            260 => self.wait(pid, p, a),
            95 => self.waitid(p, a),
            167 if a[0] == 1 => {
                if a[1] > 64 {
                    return Err(Errno(22));
                }
                p.pdeath_signal = a[1] as u32;
                Ok(Some(0))
            }
            167 if a[0] == 2 => {
                write_u32(&mut p.memory, a[1], p.pdeath_signal)?;
                Ok(Some(0))
            }
            167 if a[0] == 3 => Ok(Some(p.linux.vfs.dumpable())),
            167 if a[0] == 4 => p.linux.vfs.set_dumpable(a[1]).map(|()| Some(0)),
            167 if matches!(a[0], 36 | 37) => match a[0] {
                36 => {
                    p.subreaper = a[1] != 0;
                    for t in self.processes.values_mut().filter(|t| t.tgid == p.tgid) {
                        t.subreaper = p.subreaper;
                    }
                    Ok(Some(0))
                }
                37 => {
                    write_u32(&mut p.memory, a[1], u32::from(p.subreaper))?;
                    Ok(Some(0))
                }
                _ => Err(Errno(22)),
            },
            167 if matches!(a[0], 7 | 8 | 23 | 24 | 27 | 28 | 38 | 39 | 47) => {
                p.linux.vfs.credentials.caps.prctl(a).map(Some)
            }
            167 if !matches!(a[0], 15 | 16) => Err(Errno(22)),
            72 | 73 => self.poll(p, number, a, io),
            22 | 441 => self.epoll(p, number, a, io),
            74 => self.signalfd(p, a),
            85..=87 => self.timerfd(p, number, a),
            134 => {
                let sig = a[0] as u32;
                if sig == 0 || sig > 64 || sig == 9 || sig == 19 || a[3] != 8 {
                    return Err(Errno(22));
                }
                if a[2] != 0 {
                    Linux::check(&p.memory, a[2], 32, Access::Write)?;
                }
                let new = if a[1] != 0 {
                    Linux::check(&p.memory, a[1], 32, Access::Read)?;
                    // musl sign-extends its signed int sa_flags into the
                    // kernel's unsigned long. SA_RESETHAND sets bit 31;
                    // Linux accepts this representation and uses the low bits.
                    let flags = read_u64(&p.memory, a[1] + 8)? as u32 as u64;
                    // Linux uses the ordinary stack for SA_ONSTACK while
                    // the alternate stack is disabled.
                    if flags & !0xdc000007 != 0 {
                        return Err(Errno(22));
                    }
                    Some(Action {
                        handler: read_u64(&p.memory, a[1])?,
                        flags,
                        restorer: read_u64(&p.memory, a[1] + 16)?,
                        mask: read_u64(&p.memory, a[1] + 24)? & !UNBLOCKABLE,
                    })
                } else {
                    None
                };
                if a[2] != 0 {
                    let old = p.actions[sig as usize];
                    for (i, value) in [old.handler, old.flags, old.restorer, old.mask]
                        .into_iter()
                        .enumerate()
                    {
                        write_u64(&mut p.memory, a[2] + i as u64 * 8, value)?;
                    }
                }
                if let Some(action) = new {
                    p.actions[sig as usize] = action;
                    if action.handler == 1 || action.handler == 0 && matches!(sig, 17 | 23 | 28) {
                        p.pending.discard(sig);
                        p.shared_pending.borrow_mut().discard(sig);
                        for other in self.processes.values_mut().filter(|o| o.tgid == p.tgid) {
                            other.pending.discard(sig);
                        }
                    }
                }
                Ok(Some(0))
            }
            135 => {
                if a[3] != 8 {
                    return Err(Errno(22));
                }
                if a[2] != 0 {
                    Linux::check(&p.memory, a[2], 8, Access::Write)?;
                }
                let new = if a[1] != 0 {
                    let bits = read_u64(&p.memory, a[1])?;
                    Some(
                        match a[0] {
                            0 => p.mask | bits,
                            1 => p.mask & !bits,
                            2 => bits,
                            _ => return Err(Errno(22)),
                        } & !UNBLOCKABLE,
                    )
                } else {
                    None
                };
                if a[2] != 0 {
                    write_u64(&mut p.memory, a[2], p.mask)?;
                }
                if let Some(mask) = new {
                    p.mask = mask;
                }
                Ok(Some(0))
            }
            136 => {
                if a[1] != 8 {
                    return Err(Errno(22));
                }
                let bits = p.pending_bits() & p.mask;
                write_u64(&mut p.memory, a[0], bits)?;
                Ok(Some(0))
            }
            133 => {
                if a[1] != 8 {
                    return Err(Errno(22));
                }
                let mask = read_u64(&p.memory, a[0])? & !UNBLOCKABLE;
                let old = p.mask;
                p.mask = mask;
                p.state = State::Suspend(old);
                Ok(None)
            }
            139 => {
                Self::sigreturn(p)?;
                Ok(None)
            }
            129 => self.signal(pid, p, a[0] as i32, a[1] as u32).map(Some),
            130 => self
                .thread_signal(pid, p, a[0] as u32, None, a[1] as u32)
                .map(Some),
            131 => {
                if a[0] == 0 || a[0] > i32::MAX as u64 {
                    return Err(Errno(22));
                }
                self.thread_signal(pid, p, a[1] as u32, Some(a[0] as u32), a[2] as u32)
                    .map(Some)
            }
            154 => {
                if a[1] > i32::MAX as u64 {
                    return Err(Errno(22));
                }
                let target = if a[0] == 0 {
                    p.tgid
                } else {
                    p.linux.vfs.global_pid(a[0] as u32)?
                };
                let group = if a[1] == 0 {
                    target
                } else {
                    p.linux.vfs.global_pid(a[1] as u32).map_err(|_| Errno(1))?
                };
                let (session, parent) = if target == p.tgid {
                    (p.session, p.parent)
                } else {
                    let c = self
                        .processes
                        .get(&target)
                        .filter(|c| c.tgid == target)
                        .ok_or(Errno(3))?;
                    (c.session, c.parent)
                };
                if target != p.tgid && parent != p.tgid {
                    return Err(Errno(3));
                }
                if session != p.session || target == session {
                    return Err(Errno(1));
                }
                if group != target
                    && group != p.group
                    && !self
                        .processes
                        .values()
                        .any(|c| c.group == group && c.session == p.session)
                {
                    return Err(Errno(1));
                }
                if target == p.tgid {
                    p.group = group;
                }
                for other in self.processes.values_mut().filter(|t| t.tgid == target) {
                    other.group = group;
                }
                Ok(Some(0))
            }
            155 | 156 => {
                let id = if a[0] == 0 {
                    p.tgid
                } else {
                    p.linux.vfs.global_pid(a[0] as u32)?
                };
                let (group, session) = if id == pid {
                    (p.group, p.session)
                } else {
                    let c = self.processes.get(&id).ok_or(Errno(3))?;
                    (c.group, c.session)
                };
                Ok(Some(
                    p.linux
                        .vfs
                        .visible_pid(if number == 155 { group } else { session })
                        as u64,
                ))
            }
            157 => {
                if p.group == p.tgid || self.processes.values().any(|c| c.group == p.tgid) {
                    return Err(Errno(1));
                }
                p.session = p.tgid;
                p.group = p.tgid;
                p.linux.vfs.set_controlling_terminal(None)?;
                for t in self.processes.values_mut().filter(|t| t.tgid == p.tgid) {
                    t.session = p.session;
                    t.group = p.group;
                    t.linux.vfs.set_controlling_terminal(None)?;
                }
                Ok(Some(p.linux.vfs.visible_pid(p.tgid) as u64))
            }
            101 | 115 => {
                if number == 115 && (!matches!(a[0], 0 | 1) || a[1] & !1 != 0) {
                    return Err(Errno(22));
                }
                let (request, remaining) = if number == 101 {
                    (a[0], a[1])
                } else {
                    (a[2], if a[1] & 1 == 0 { a[3] } else { 0 })
                };
                let sec = read_u64(&p.memory, request)?;
                let ns = read_u64(&p.memory, request + 8)?;
                if ns >= 1_000_000_000 || sec > i64::MAX as u64 {
                    return Err(Errno(22));
                }
                if remaining != 0 {
                    Linux::check(&p.memory, remaining, 16, Access::Write)?;
                }
                let mut duration = Duration::new(sec, ns as u32);
                if number == 115 && a[1] & 1 != 0 {
                    let now = if a[0] == 0 {
                        std::time::SystemTime::now()
                            .duration_since(std::time::UNIX_EPOCH)
                            .map_err(|_| Errno(5))?
                    } else {
                        self.started.elapsed()
                    };
                    duration = duration.saturating_sub(now);
                }
                p.state = State::Sleep(
                    Instant::now().checked_add(duration).ok_or(Errno(22))?,
                    remaining,
                );
                Ok(None)
            }
            113 | 114 => {
                crate::worker_syscalls::clock(&mut p.memory, number, a[0], a[1], self.started)
                    .map(Some)
            }
            179 => {
                Linux::check(&p.memory, a[0], 112, Access::Write)?;
                let used = self.other_memory(p.vm_id) + p.memory.mapped_bytes();
                let mut out = [0; 112];
                for (offset, value) in [
                    (0, self.started.elapsed().as_secs()),
                    (32, self.memory_limits.total as u64),
                    (40, self.memory_limits.total.saturating_sub(used) as u64),
                ] {
                    out[offset..offset + 8].copy_from_slice(&value.to_le_bytes());
                }
                for (i, load) in self.load.iter().enumerate() {
                    out[8 + i * 8..16 + i * 8]
                        .copy_from_slice(&((load * 65536.0) as u64).to_le_bytes());
                }
                out[80..82].copy_from_slice(&((self.processes.len() + 1) as u16).to_le_bytes());
                out[104..108].copy_from_slice(&1u32.to_le_bytes());
                p.memory.write(a[0], &out).map_err(|_| Errno(14))?;
                Ok(Some(0))
            }
            31 => self.ioprio_get(pid, p, a[0], a[1]).map(Some),
            132 => Self::sigaltstack(p, a[0], a[1]).map(Some),
            283 => Self::membarrier(p, a[0], a[1]).map(Some),
            163 | 164 | 261 => self.rlimit(p, number, a).map(Some),
            124 => Ok(Some(0)),
            203 if matches!(p.state, State::Blocked(203, _))
                && p.linux.vfs.local_socket(a[0] as u32)?.is_none() =>
            {
                if p.linux.resume_connect(a[0] as u32)? {
                    Ok(Some(0))
                } else if p
                    .socket_deadline
                    .is_some_and(|deadline| Instant::now() >= deadline)
                {
                    Err(Errno(115))
                } else {
                    Ok(None)
                }
            }
            63 | 65 if p.linux.vfs.signalfd_mask(a[0] as u32)?.is_some() => {
                self.read_signalfd(p, number, a)
            }
            63 | 64 | 65 | 66 | 29
                if (number != 29 || a[1] as u32 != 0x5421)
                    && p.linux.vfs.fd_kind(a[0] as u32)? == FdKind::Terminal =>
            {
                self.terminal_call(pid, p, number, a, io)
            }
            _ => {
                if number == 56 {
                    if let Some(fd) = p.pending_open {
                        if p.linux.vfs.fifo_open_ready(fd)? {
                            p.pending_open = None;
                            return Ok(Some(fd as u64));
                        }
                        return Ok(None);
                    }
                }
                let trace = p.linux.trace;
                p.linux.trace = false;
                let outcome = p.linux.dispatch(&mut p.cpu, &mut p.memory, io);
                p.linux.trace = trace;
                if let SyscallOutcome::Exit(code) = outcome {
                    self.exit(pid, p, (code as i32) << 8);
                    return Ok(None);
                }
                let raw = p.cpu.x(0);
                // A blocking FIFO open reserves a hidden descriptor. Resolve its
                // rendezvous before descriptor-based PTY inspection, which must
                // reject that descriptor while the open is still pending.
                if number == 56
                    && raw < MAX_FDS as u64
                    && !p.linux.vfs.fifo_open_ready(raw as u32)?
                {
                    p.pending_open = Some(raw as u32);
                    return Ok(None);
                }
                if number == 56
                    && raw < MAX_FDS as u64
                    && a[2] & 0x100 == 0
                    && pid == p.session
                    && p.linux.vfs.controlling_terminal.is_none()
                {
                    if let Some(end) = p.linux.vfs.pty(raw as u32)?.filter(|end| !end.master) {
                        let mut tty = end.pair.terminal.borrow_mut();
                        if tty.session == 0 {
                            tty.session = p.session;
                            tty.foreground = p.group;
                            p.linux.vfs.set_controlling_terminal(Some(end.pair.id))?;
                        }
                    }
                }
                if number == 25 && a[1] == 7 && raw == error(Errno(11)) {
                    let mut owner = p.linux.record_lock_holder(&p.memory, a)?;
                    let mut visited = Vec::new();
                    while let Some(id) = owner {
                        if id == p.tgid {
                            return Err(Errno(35));
                        }
                        if visited.contains(&id) {
                            break;
                        }
                        visited.push(id);
                        owner = self.processes.values().find_map(|other| {
                            if other.tgid == id {
                                if let State::Blocked(25, a) = other.state {
                                    if a[1] == 7 {
                                        return other
                                            .linux
                                            .record_lock_holder(&other.memory, a)
                                            .ok()
                                            .flatten();
                                    }
                                }
                            }
                            None
                        });
                    }
                }
                let per_call_nonblock = match number {
                    76 => a[5] & 2 != 0 || p.linux.vfs.flags(a[2] as u32)? & O_NONBLOCK != 0,
                    206 | 207 => a[3] & 0x40 != 0,
                    211 | 212 => a[2] & 0x40 != 0,
                    _ => false,
                };
                if number == 116 && a[0] as u32 == 2 && raw == error(Errno(11)) {
                    return Ok(None);
                }
                if ((raw == error(Errno(11))
                    && matches!(
                        number,
                        63 | 64 | 65 | 66 | 71 | 76 | 202 | 203 | 242 | 206 | 207 | 211 | 212
                    )
                    && !per_call_nonblock)
                    || (raw == error(Errno(115)) && number == 203)
                    || (raw == error(Errno(11)) && number == 32 && a[1] & 4 == 0)
                    || (raw == error(Errno(11)) && number == 25 && a[1] == 7))
                    && (matches!(number, 25 | 32)
                        || p.linux.vfs.flags(a[0] as u32)? & O_NONBLOCK == 0)
                {
                    if !matches!(p.state, State::Blocked(..)) {
                        p.socket_deadline = match number {
                            63 | 65 | 202 | 242 | 207 | 212 => Some(true),
                            64 | 66 | 203 | 206 | 211 => Some(false),
                            _ => None,
                        }
                        .and_then(|receive| {
                            p.linux
                                .vfs
                                .socket_fd(a[0] as u32)
                                .ok()
                                .and_then(|socket| socket.io_timeout(receive))
                        })
                        .and_then(|duration| Instant::now().checked_add(duration));
                    }
                    if p.socket_deadline
                        .is_some_and(|deadline| Instant::now() >= deadline)
                    {
                        return Ok(Some(raw));
                    }
                    Ok(None)
                } else {
                    Ok(Some(raw))
                }
            }
        }
    }
    fn call(
        &mut self,
        pid: u32,
        p: &mut Process,
        number: u32,
        a: [u64; 6],
        io: &mut impl GuestOutput,
    ) {
        let began = self.profile_syscalls.then(Instant::now);
        self.syscalls += 1;
        if !matches!(p.state, State::Blocked(..)) && self.filter_syscall(pid, p, number, a) {
            return;
        }
        p.linux.vfs.set_process_context(pid);
        p.linux.vfs.set_lock_owner(p.tgid);
        p.linux.vfs.set_file_size_limit(p.file_size_limit.get().0);
        let proc_path = match number {
            27 => Some((trames_vfs::AT_FDCWD, a[1])),
            43 | 45 | 221 => Some((trames_vfs::AT_FDCWD, a[0])),
            48 | 53 | 54 | 56 | 78 | 79 | 88 | 281 | 291 | 437 | 439 | 452 => {
                Some((a[0] as i32, a[1]))
            }
            _ => None,
        };
        let reads_proc = matches!(number, 39..=41) || proc_path.is_some_and(|(dirfd, address)| {
            p.memory.read_c_string(address, 4096).is_ok_and(|path| {
                p.linux.vfs.accesses_proc(dirfd, &path)
            })
        }) || (matches!(number, 44 | 61 | 63 | 65 | 67 | 69 | 78 | 80)
            && p.linux.vfs.fd_kind(a[0] as u32).is_ok_and(|kind| {
                matches!(kind, FdKind::Node(path) if path == b"/proc" || path.starts_with(b"/proc/"))
            }));
        if reads_proc {
            self.proc_refreshes += 1;
            self.refresh_proc(pid, p);
        }
        let other_bytes = self.other_memory(p.vm_id);
        let _ = p.memory.set_limit(
            self.memory_limits
                .per_process
                .min(self.memory_limits.total.saturating_sub(other_bytes)),
        );
        let was_blocked = matches!(p.state, State::Blocked(..));
        p.cpu.set_x(8, number as u64);
        put_args(&mut p.cpu, a);
        let ids = |c: &trames_vfs::Credentials| [c.euid, c.egid, c.fsuid, c.fsgid];
        let before_ids = ids(&p.linux.vfs.credentials);
        let result = self.syscall(pid, p, number, a, io);
        if ids(&p.linux.vfs.credentials) != before_ids {
            p.pdeath_signal = 0;
        }
        self.sync_resources(p);
        let raw = match result {
            Ok(Some(raw)) => {
                p.cpu.set_x(0, raw);
                p.state = State::Ready;
                p.socket_deadline = None;
                Some(raw)
            }
            Ok(None) => {
                if matches!(p.state, State::Ready) && !matches!(number, 221 | 281 | 139 | 93 | 94) {
                    p.state = State::Blocked(number, a);
                    put_args(&mut p.cpu, a);
                }
                None
            }
            Err(e) => {
                p.socket_deadline = None;
                if matches!(number, 22 | 72 | 73 | 441) {
                    // A failed poll ends its wait just as a successful return
                    // does. Do not leak its deadline or temporary signal mask
                    // into a later read, poll, or runnable CPU epoch.
                    p.poll_deadline = None;
                    if let Some(mask) = p.poll_mask.take() {
                        p.mask = mask;
                    }
                }
                if matches!(e.0, 38 | 95) {
                    p.linux.vfs.log_syscall(
                        number as u64,
                        p.cpu.pc.saturating_sub(4),
                        a,
                        e.0,
                        &p.linux.command_name,
                    );
                }
                let raw = error(e);
                p.cpu.set_x(0, raw);
                p.state = State::Ready;
                Some(raw)
            }
        };
        let signal_pipe = match number {
            64 | 66 | 71 | 76 => true,
            206 => a[3] & 0x4000 == 0,
            211 => a[2] & 0x4000 == 0,
            _ => false,
        };
        if signal_pipe && raw == Some(error(Errno(32))) {
            Self::queue_one(pid, p, 13, pid);
        }
        if p.linux.vfs.take_file_size_signal() {
            Self::queue_one(pid, p, 25, pid);
        }
        for target in p.linux.vfs.take_cgroup_kills() {
            if target == pid {
                Self::queue_one(pid, p, 9, 0);
            } else if let Some(task) = self.processes.get_mut(&target) {
                Self::queue_one(target, task, 9, 0);
            }
        }
        if p.linux.trace && !(raw.is_none() && matches!(p.state, State::Blocked(..)) && was_blocked)
        {
            eprintln!(
                "trames pid {pid} syscall {number}({:#x}, {:#x}, {:#x}) = {}",
                a[0],
                a[1],
                a[2],
                raw.map_or_else(|| "suspended/replaced".into(), |v| (v as i64).to_string())
            );
        }
        if let Some(began) = began {
            self.syscall_total += began.elapsed();
        }
    }
    fn readiness(
        &self,
        p: &Process,
        fd: u32,
        events: u16,
        io: &mut impl GuestOutput,
    ) -> Result<u16, Errno> {
        let kind = p.linux.vfs.fd_kind(fd);
        let ready = match kind {
            Ok(FdKind::Stdin) => {
                if io.stdin_ready().map_err(|_| Errno(5))? {
                    events & 1
                } else {
                    0
                }
            }
            Ok(FdKind::Terminal) => {
                if let Some(end) = p.linux.vfs.pty(fd)? {
                    return Ok(end.pair.poll(end.master, events));
                }
                let t = self.terminal.as_ref().ok_or(Errno(25))?;
                (events & 4) | if t.readable() { events & 1 } else { 0 }
            }
            _ => p.linux.vfs.poll_signals(fd, events, p.pending_bits()),
        };
        Ok(ready)
    }
    fn poll(
        &mut self,
        p: &mut Process,
        number: u32,
        a: [u64; 6],
        io: &mut impl GuestOutput,
    ) -> Result<Option<u64>, Errno> {
        let timeout = if number == 73 { a[2] } else { a[4] };
        let mut expired = false;
        if timeout != 0 {
            let sec = read_u64(&p.memory, timeout)?;
            let ns = read_u64(&p.memory, timeout + 8)?;
            if sec > i64::MAX as u64 || ns >= 1_000_000_000 {
                return Err(Errno(22));
            }
            if p.poll_deadline.is_none() {
                p.poll_deadline = Some(
                    Instant::now()
                        .checked_add(Duration::new(sec, ns as u32))
                        .ok_or(Errno(22))?,
                );
            }
            expired = p.poll_deadline.is_some_and(|d| Instant::now() >= d);
        }
        let mask = if number == 73 {
            if a[3] != 0 {
                if a[4] != 8 {
                    return Err(Errno(22));
                }
                Some(read_u64(&p.memory, a[3])?)
            } else {
                None
            }
        } else if a[5] != 0 {
            let ptr = read_u64(&p.memory, a[5])?;
            let len = read_u64(&p.memory, a[5] + 8)?;
            if ptr != 0 {
                if len != 8 {
                    return Err(Errno(22));
                }
                Some(read_u64(&p.memory, ptr)?)
            } else {
                None
            }
        } else {
            None
        };
        let mut count = 0;
        let mut outputs = Vec::new();
        if number == 73 {
            if a[1] > 256 {
                return Err(Errno(22));
            }
            Linux::check(&p.memory, a[0], a[1] as usize * 8, Access::Write)?;
            for i in 0..a[1] {
                let base = a[0] + i * 8;
                let fd = read_u32(&p.memory, base)? as i32;
                let event = read_u32(&p.memory, base + 4)? as u16;
                let ready = if fd < 0 {
                    0
                } else {
                    self.readiness(p, fd as u32, event, io)?
                };
                if ready != 0 {
                    count += 1;
                }
                outputs.push((base + 6, ready.to_le_bytes().to_vec()));
            }
        } else {
            if a[0] > 256 {
                return Err(Errno(22));
            }
            let size = a[0].div_ceil(64) as usize * 8;
            for (idx, event) in [(1, 1u16), (2, 4), (3, 2)] {
                if a[idx] == 0 {
                    continue;
                }
                Linux::check(&p.memory, a[idx], size, Access::Write)?;
                let mut original = vec![0; size];
                p.memory
                    .read(a[idx], &mut original)
                    .map_err(|_| Errno(14))?;
                let mut result = vec![0; size];
                for fd in 0..a[0] as u32 {
                    if original[fd as usize / 8] & (1 << (fd % 8)) == 0 {
                        continue;
                    }
                    let ready = self.readiness(p, fd, event, io)?;
                    if ready & 32 != 0 {
                        return Err(Errno(9));
                    }
                    if ready & (event | 8 | 16) != 0 && event != 2 {
                        result[fd as usize / 8] |= 1 << (fd % 8);
                        count += 1;
                    }
                }
                outputs.push((a[idx], result));
            }
        }
        if count != 0 || expired {
            for (ptr, bytes) in outputs {
                p.memory.write(ptr, &bytes).map_err(|_| Errno(14))?;
            }
            if let Some(old) = p.poll_mask.take() {
                p.mask = old;
            }
            p.poll_deadline = None;
            Ok(Some(count))
        } else {
            if let Some(mask) = mask {
                if p.poll_mask.is_none() {
                    p.poll_mask = Some(p.mask);
                }
                p.mask = mask & !UNBLOCKABLE;
            }
            Ok(None)
        }
    }
    fn terminal_signal(
        &mut self,
        pid: u32,
        p: &mut Process,
        session: u32,
        group: u32,
        signal: u32,
    ) {
        if session == 0 || group == 0 {
            return;
        }
        if p.session == session && p.group == group {
            Self::queue_one(pid, p, signal, 0);
        }
        for (&id, other) in self.processes.iter_mut() {
            if other.session == session && other.group == group {
                Self::queue_one(id, other, signal, 0);
            }
        }
    }
    fn terminal_call(
        &mut self,
        pid: u32,
        p: &mut Process,
        number: u32,
        a: [u64; 6],
        io: &mut impl GuestOutput,
    ) -> Result<Option<u64>, Errno> {
        let flags = p.linux.vfs.flags(a[0] as u32)?;
        if flags & trames_vfs::O_PATH != 0 {
            return Err(Errno(9));
        }
        if matches!(number, 63 | 65) && flags & 3 == 1
            || matches!(number, 64 | 66) && flags & 3 == 0
        {
            return Err(Errno(9));
        }
        let endpoint = p.linux.vfs.pty(a[0] as u32)?;
        let master = endpoint.as_ref().is_some_and(|end| end.master);
        let id = endpoint.as_ref().map_or(0, |end| end.pair.id);
        if number == 29 {
            let request = a[1] as u32;
            if matches!(
                request,
                0x80045430 | 0x40045431 | 0x80045439 | 0x5420 | 0x80045438 | 0x5441
            ) {
                let end = endpoint
                    .as_ref()
                    .filter(|end| end.master)
                    .ok_or(Errno(25))?;
                match request {
                    0x80045430 => write_u32(&mut p.memory, a[2], end.pair.number)?,
                    0x40045431 => end.pair.locked.set(read_u32(&p.memory, a[2])? != 0),
                    0x80045439 => write_u32(&mut p.memory, a[2], u32::from(end.pair.locked.get()))?,
                    0x5420 => {
                        end.pair.packet.set(read_u32(&p.memory, a[2])? != 0);
                        end.pair.packet_status.set(0);
                    }
                    0x80045438 => write_u32(&mut p.memory, a[2], u32::from(end.pair.packet.get()))?,
                    0x5441 => {
                        return p
                            .linux
                            .vfs
                            .open_pty_peer(a[0] as u32, a[2] as u32)
                            .map(|fd| Some(fd as u64))
                    }
                    _ => unreachable!(),
                }
                return Ok(Some(0));
            }
        }
        let mut dynamic = endpoint.as_ref().map(|end| end.pair.terminal.borrow_mut());
        let tty = match dynamic.as_mut() {
            Some(tty) => &mut **tty,
            None => self.terminal.as_mut().ok_or(Errno(25))?,
        };
        if number == 63 || number == 65 {
            let (parts, count) = if number == 65 {
                Linux::readv_parts(&p.memory, a)?
            } else {
                let count = a[2].min(MAX_WRITE as u64) as usize;
                Linux::check(&p.memory, a[1], count, Access::Write)?;
                (vec![(a[1], count)], count)
            };
            if count == 0 {
                return Ok(Some(0));
            }
            if master {
                drop(dynamic);
                return match endpoint.as_ref().expect("master").pair.master_read(count) {
                    Ok(bytes) => {
                        Linux::scatter(&mut p.memory, &parts, &bytes)?;
                        Ok(Some(bytes.len() as u64))
                    }
                    Err(Errno(11)) if p.linux.vfs.flags(a[0] as u32)? & O_NONBLOCK == 0 => Ok(None),
                    Err(e) => Err(e),
                };
            }
            if endpoint
                .as_ref()
                .is_some_and(|end| !end.pair.master_alive.get())
            {
                return Ok(Some(0));
            }
            if p.linux.vfs.controlling_terminal == Some(id)
                && p.session == tty.session
                && p.group != tty.foreground
            {
                if p.mask & (1 << 20) != 0 || p.actions[21].handler == 1 {
                    return Err(Errno(5));
                }
                let group = p.group;
                let session = tty.session;
                self.terminal_signal(pid, p, session, group, 21);
                return Ok(None);
            }
            match tty.read(count) {
                Ok(bytes) => {
                    Linux::scatter(&mut p.memory, &parts, &bytes)?;
                    Ok(Some(bytes.len() as u64))
                }
                Err(Errno(11)) if p.linux.vfs.flags(a[0] as u32)? & O_NONBLOCK == 0 => Ok(None),
                Err(e) => Err(e),
            }
        } else if number == 64 || number == 66 {
            let mut bytes = Vec::new();
            if number == 64 {
                let count = a[2].min(MAX_WRITE as u64) as usize;
                Linux::check(&p.memory, a[1], count, Access::Read)?;
                bytes.resize(count, 0);
                p.memory.read(a[1], &mut bytes).map_err(|_| Errno(14))?;
            } else {
                if a[2] > 1024 {
                    return Err(Errno(22));
                }
                Linux::check(&p.memory, a[1], a[2] as usize * 16, Access::Read)?;
                let mut vectors = Vec::new();
                let mut total = 0;
                for i in 0..a[2] {
                    let ptr = read_u64(&p.memory, a[1] + i * 16)?;
                    let len = read_u64(&p.memory, a[1] + i * 16 + 8)?;
                    if len > i64::MAX as u64 {
                        return Err(Errno(22));
                    }
                    let count = len.min((MAX_WRITE - total) as u64) as usize;
                    Linux::check(&p.memory, ptr, count, Access::Read)?;
                    vectors.push((ptr, count));
                    total += count;
                }
                for (ptr, count) in vectors {
                    let start = bytes.len();
                    bytes.resize(start + count, 0);
                    p.memory
                        .read(ptr, &mut bytes[start..])
                        .map_err(|_| Errno(14))?;
                }
            }
            if master {
                let count = bytes.len().min(tty.input_capacity());
                if count == 0 && !bytes.is_empty() {
                    return if p.linux.vfs.flags(a[0] as u32)? & O_NONBLOCK != 0 {
                        Err(Errno(11))
                    } else {
                        Ok(None)
                    };
                }
                let stopped = tty.flow_stopped;
                let signals = tty.input(&bytes[..count]);
                let group = tty.foreground;
                let session = tty.session;
                if stopped != tty.flow_stopped {
                    tty.output_epoch = tty.output_epoch.wrapping_add(1);
                    let pair = &endpoint.as_ref().expect("master").pair;
                    pair.packet_status
                        .set(pair.packet_status.get() | if tty.flow_stopped { 4 } else { 8 });
                }
                drop(dynamic);
                endpoint.as_ref().expect("master").pair.flush_echo();
                for sig in signals {
                    if group != 0 {
                        self.terminal_signal(pid, p, session, group, sig);
                    }
                }
                return Ok(Some(count as u64));
            }
            if endpoint
                .as_ref()
                .is_some_and(|end| !end.pair.master_alive.get())
            {
                return Err(Errno(5));
            }
            if p.linux.vfs.controlling_terminal == Some(id)
                && p.session == tty.session
                && tty.lflag() & 256 != 0
                && p.group != tty.foreground
                && p.mask & (1 << 21) == 0
                && p.actions[22].handler != 1
            {
                let group = p.group;
                let session = tty.session;
                self.terminal_signal(pid, p, session, group, 22);
                return Ok(None);
            }
            let count = match tty.queue_output(&bytes) {
                Err(Errno(11)) if p.linux.vfs.flags(a[0] as u32)? & O_NONBLOCK == 0 => {
                    return Ok(None)
                }
                result => result?,
            };
            let _ = io;
            Ok(Some(count as u64))
        } else {
            let request = a[1] as u32;
            if matches!(request, 0x540f | 0x5410 | 0x5429)
                && !master
                && (p.session != tty.session || p.linux.vfs.controlling_terminal != Some(id))
            {
                return Err(Errno(25));
            }
            if !master
                && p.linux.vfs.controlling_terminal == Some(id)
                && p.session == tty.session
                && p.group != tty.foreground
                && matches!(request, 0x5402..=0x5404 | 0x5410)
                && p.mask & (1 << 21) == 0
                && p.actions[22].handler != 1
            {
                let group = p.group;
                let session = tty.session;
                self.terminal_signal(pid, p, session, group, 22);
                return Ok(None);
            }
            match request {
                0x5401 => {
                    Linux::check(&p.memory, a[2], 36, Access::Write)?;
                    p.memory
                        .write(a[2], &tty.attributes)
                        .map_err(|_| Errno(14))?;
                }
                0x5402..=0x5404 => {
                    let mut attr = [0; 36];
                    p.memory.read(a[2], &mut attr).map_err(|_| Errno(14))?;
                    if attr[22] != 0 {
                        return Err(Errno(95));
                    }
                    tty.attributes = attr;
                    if request == 0x5404 {
                        tty.flush_input();
                    }
                }
                0x540f => {
                    p.memory
                        .write(a[2], &p.linux.vfs.visible_pid(tty.foreground).to_le_bytes())
                        .map_err(|_| Errno(14))?;
                }
                0x5410 => {
                    let group = read_u32(&p.memory, a[2])?;
                    if group == 0 || group > i32::MAX as u32 {
                        return Err(Errno(22));
                    }
                    let group = p.linux.vfs.global_pid(group).map_err(|_| Errno(1))?;
                    if group != p.group
                        && !self
                            .processes
                            .values()
                            .any(|c| c.group == group && c.session == p.session)
                    {
                        return Err(Errno(1));
                    }
                    tty.foreground = group;
                }
                0x5413 => {
                    p.memory.write(a[2], &tty.window).map_err(|_| Errno(14))?;
                }
                0x5414 => {
                    let mut window = [0; 8];
                    p.memory.read(a[2], &mut window).map_err(|_| Errno(14))?;
                    let changed = tty.window != window;
                    tty.window = window;
                    if changed {
                        let group = tty.foreground;
                        let session = tty.session;
                        self.terminal_signal(pid, p, session, group, 28);
                    }
                }
                0x541b => {
                    p.memory
                        .write(
                            a[2],
                            &(if master {
                                tty.output_queue.len()
                            } else {
                                tty.available()
                            } as u32)
                                .to_le_bytes(),
                        )
                        .map_err(|_| Errno(14))?;
                }
                0x540e => {
                    if master
                        || pid != p.session
                        || p.linux
                            .vfs
                            .controlling_terminal
                            .is_some_and(|old| old != id)
                    {
                        return Err(Errno(1));
                    }
                    if tty.session != 0
                        && tty.session != p.session
                        && self.processes.values().any(|other| {
                            other.session == tty.session
                                && other.linux.vfs.controlling_terminal == Some(id)
                        })
                        && !(a[2] == 1 && p.linux.vfs.credentials.has_cap(capabilities::SYS_ADMIN))
                    {
                        return Err(Errno(1));
                    }
                    tty.session = p.session;
                    tty.foreground = p.group;
                    p.linux.vfs.set_controlling_terminal(Some(id))?;
                }
                0x5429 => write_u32(&mut p.memory, a[2], p.linux.vfs.visible_pid(tty.session))?,
                0x5422 => {
                    if p.linux.vfs.controlling_terminal != Some(id) {
                        return Err(Errno(25));
                    }
                    p.linux.vfs.set_controlling_terminal(None)?;
                    if pid == tty.session {
                        tty.session = 0;
                    }
                }
                0x540b => {
                    if a[2] > 2 {
                        return Err(Errno(22));
                    }
                    if a[2] == 0 || a[2] == 2 {
                        tty.flush_input();
                    }
                    if a[2] == 1 || a[2] == 2 {
                        tty.output_queue.clear();
                    }
                }
                _ => return Err(Errno(25)),
            }
            Ok(Some(0))
        }
    }
    fn pump_guest_ptys(&mut self) {
        let pairs = self
            .processes
            .values()
            .next()
            .map(|p| p.linux.vfs.live_ptys())
            .unwrap_or_default();
        for pair in pairs {
            pair.flush_echo();
            if pair.host_resize_pending.replace(false) {
                pair.host_signals.set(pair.host_signals.get() | (1 << 27));
            }
            let signals = pair.host_signals.replace(0);
            if signals != 0 {
                let tty = pair.terminal.borrow();
                for (&pid, process) in &mut self.processes {
                    if tty.session != 0
                        && process.session == tty.session
                        && process.group == tty.foreground
                    {
                        for sig in 1..=64 {
                            if signals & (1 << (sig - 1)) != 0 {
                                Self::queue_one(pid, process, sig, 0);
                            }
                        }
                    }
                }
            }
            if pair.master_alive.get() || pair.hangup_notified.replace(true) {
                continue;
            }
            let mut tty = pair.terminal.borrow_mut();
            let (session, group) = (tty.session, tty.foreground);
            tty.flush_input();
            tty.eof = true;
            tty.session = 0;
            tty.foreground = 0;
            drop(tty);
            for (&pid, process) in self.processes.iter_mut() {
                if process.linux.vfs.controlling_terminal == Some(pair.id) {
                    process
                        .linux
                        .vfs
                        .set_controlling_terminal(None)
                        .expect("detach");
                }
                if session != 0
                    && process.session == session
                    && (pid == session || process.group == group)
                {
                    Self::queue_one(pid, process, 1, 0);
                    Self::queue_one(pid, process, 18, 0);
                }
            }
        }
    }
    fn pump_terminal(&mut self, io: &mut impl GuestOutput) -> Result<bool, RuntimeError> {
        let Some(tty) = self.terminal.as_mut() else {
            return Ok(false);
        };
        let mut input = [0; 4096];
        let mut progress = false;
        let capacity = tty.input_capacity().min(input.len());
        let mut signals = if capacity == 0 {
            Vec::new()
        } else {
            match io.poll_stdin(&mut input[..capacity]) {
                Ok(0) => {
                    let changed = !tty.eof;
                    tty.eof = true;
                    progress |= changed;
                    Vec::new()
                }
                Ok(n) if n <= capacity => {
                    progress = true;
                    tty.input(&input[..n])
                }
                Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => Vec::new(),
                _ => return Err(RuntimeError::Arguments("terminal master input failed")),
            }
        };
        for _ in 0..16 {
            match io.terminal_event() {
                Some(TerminalEvent::Resize { rows, columns }) => {
                    let old = tty.window;
                    tty.resize(rows, columns);
                    if old != tty.window {
                        signals.push(28);
                        progress = true;
                    }
                }
                Some(TerminalEvent::Signal(sig)) if sig > 0 && sig <= 64 => {
                    signals.push(sig);
                    progress = true;
                }
                None => break,
                _ => {}
            }
        }
        let group = tty.foreground;
        let echo = std::mem::take(&mut tty.echo);
        if !echo.is_empty() {
            let consumed = match tty.queue_output(&echo) {
                Ok(n) => n,
                Err(Errno(11)) => 0,
                Err(_) => return Err(RuntimeError::Arguments("terminal echo failed")),
            };
            tty.echo = echo[consumed..].to_vec();
        }
        let bytes: Vec<_> = if tty.flow_stopped {
            Vec::new()
        } else {
            tty.output_queue.iter().copied().take(4096).collect()
        };
        if !bytes.is_empty() {
            match io.write(OutputFd::Stdout, &bytes) {
                Ok(n) if n <= bytes.len() => {
                    tty.drain_output(n);
                    progress |= n != 0;
                }
                Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {}
                _ => return Err(RuntimeError::Arguments("terminal master output failed")),
            }
        }
        for sig in signals {
            for (&pid, p) in &mut self.processes {
                if p.group == group {
                    Self::queue_one(pid, p, sig, 0);
                }
            }
        }
        Ok(progress)
    }
    fn finish_slice(
        &mut self,
        id: u32,
        p: &mut Process,
        outcome: SliceOutcome,
        io: &mut impl GuestOutput,
        account: bool,
    ) -> bool {
        let mut immediate_return = false;
        match outcome {
            SliceOutcome::Fault(e) => {
                if p.linux.trace {
                    eprintln!("trames pid {id}: {e}");
                }
                if account {
                    self.instructions += p.cpu.fault_retired();
                }
                self.synchronous_fault(id, p, &e);
            }
            SliceOutcome::Yield { instructions } => {
                if account {
                    self.instructions += instructions
                }
            }
            SliceOutcome::NativeCall {
                instructions,
                pc,
                module,
                function,
            } => {
                p.cpu.clear_exclusive();
                if account {
                    self.instructions += instructions
                };
                let began = Instant::now();
                let a = [p.cpu.x(0), p.cpu.x(1), p.cpu.x(2)];
                // Views can defer before effects when code changes during
                // validation or backing writes need coordinator authority. A
                // joined coordinator revalidates with a bounded retry.
                let mut result = self.native_call(p, pc, module, function, a);
                for _ in 0..2 {
                    if !matches!(result, Err(trames_accel::native::Error::Checkpoint)) {
                        break;
                    }
                    result = self.native_call(p, pc, module, function, a);
                }
                match result {
                    Ok(Some(value)) => {
                        p.cpu.set_x(0, value.value);
                        p.cpu.set_x(1, u64::from(value.fallback));
                        p.cpu.pc = pc + 4;
                        self.instructions += 1;
                        immediate_return = true;
                    }
                    Ok(None) => {}
                    Err(error) => {
                        use trames_accel::native::Error;
                        let fault = match error {
                            Error::Memory(error) => trames_cpu::CpuFault::Memory { pc, error },
                            Error::InvalidEntry | Error::Checkpoint => {
                                p.linux.vfs.log(4, &format!("pid={id} NativeCall rejected module={module:#x} function={function} pc={pc:#x} comm={:?}", String::from_utf8_lossy(&p.linux.command_name)));
                                trames_cpu::CpuFault::Trap {
                                    pc,
                                    instruction: trames_accel::native::INSTRUCTION,
                                    signal: 4,
                                    code: 1,
                                }
                            }
                        };
                        self.synchronous_fault(id, p, &fault);
                    }
                }
                let elapsed = began.elapsed();
                p.cpu_time += elapsed;
                self.cpu_total += elapsed;
                self.per_cpu[p.last_cpu] += elapsed;
            }
            SliceOutcome::Syscall { instructions } => {
                p.cpu.clear_exclusive();
                if account {
                    self.instructions += instructions
                };
                let number = p.cpu.x(8) as u32;
                let a = args(&p.cpu);
                self.call(id, p, number, a, io);
            }
        }
        immediate_return
    }
    fn parallel_round(
        &mut self,
        io: &mut impl GuestOutput,
        limit: u64,
    ) -> Result<bool, RuntimeError> {
        if self
            .processes
            .get(&1)
            .is_some_and(|p| matches!(p.state, State::Zombie(_)))
            && !self
                .processes
                .values()
                .any(|p| p.tgid == 1 && !matches!(p.state, State::Zombie(_)))
        {
            return Ok(false);
        }
        if self.cpu_count < 2
            || self
                .processes
                .iter()
                .filter(|(id, p)| matches!(p.state, State::Ready) && !p.frozen(**id))
                .count()
                < 2
        {
            return Ok(false);
        }
        // All kernel effects precede dispatch. No syscall/signal/native callback
        // can change mappings or borrowed bytes until every CPU has returned.
        let ids: Vec<_> = self.processes.keys().copied().collect();
        for id in ids {
            let Some(mut p) = self.processes.remove(&id) else {
                continue;
            };
            if !matches!(p.state, State::Zombie(_)) && !p.frozen(id) {
                self.resume_gpu(id, &mut p);
                self.resume_event(&mut p);
                if self.deliver(id, &mut p).is_err() {
                    self.exit(id, &mut p, 11);
                }
                self.sync_resources(&p);
                match p.state.clone() {
                    State::Blocked(number, a) => self.call(id, &mut p, number, a, io),
                    State::Sleep(deadline, _) if Instant::now() >= deadline => {
                        p.state = State::Ready;
                        p.cpu.set_x(0, 0);
                    }
                    _ => {}
                }
            }
            let actual = p.rekey.take().unwrap_or(id);
            if !p.auto_reap || actual == 1 {
                self.processes.insert(actual, p);
            }
        }
        let mut ready: Vec<_> = self
            .processes
            .iter()
            .filter(|(id, p)| matches!(p.state, State::Ready) && !p.frozen(**id))
            .map(|(&id, _)| id)
            .collect();
        let rotate = ready.partition_point(|id| *id <= self.next_parallel_pid);
        ready.rotate_left(rotate);
        let mut chosen = assign_cpus(
            ready
                .into_iter()
                .map(|id| (id, self.processes[&id].affinity)),
            self.cpu_count,
        );
        let remaining = limit.saturating_sub(self.instructions);
        chosen.truncate(remaining.min(chosen.len() as u64) as usize);
        if chosen.len() < 2 {
            return Ok(false);
        }
        // Resolve deadlines while kernel/backing objects are quiescent. Workers
        // then run continuously; only an event, a fairness/timer/control poll,
        // or the shared session budget asks all CPUs to return.
        let timeout = self.parallel_timeout();
        let began = Instant::now();
        let mut views = Vec::new();
        for &(id, slot) in &chosen {
            let p = self.processes.get_mut(&id).expect("ready process");
            let Some(view) = p.memory.parallel_view(&mut self.parallel_memory) else {
                return Ok(false);
            };
            views.push((id, slot, view));
        }
        self.parallel_memory_time += began.elapsed();
        for &(_, slot) in &chosen {
            self.workers
                .ensure(slot)
                .map_err(|_| RuntimeError::Arguments("cannot start CPU worker"))?;
        }
        let control = std::sync::Arc::new(crate::workers::Control::new(remaining));
        let began = Instant::now();
        for (id, slot, view) in views {
            let p = self.processes.get_mut(&id).expect("ready process");
            if p.last_cpu != slot {
                p.cpu.clear_exclusive();
            }
            let cpu = std::mem::take(&mut p.cpu);
            self.workers
                .dispatch(
                    slot,
                    crate::workers::Work {
                        pid: id,
                        cpu,
                        memory: view,
                        control: control.clone(),
                        syscalls: (p.seccomp.mode() == 0 && !p.linux.trace).then(|| {
                            let c = &p.linux.vfs.credentials;
                            crate::worker_syscalls::Snapshot {
                                identities: [
                                    p.linux.vfs.visible_pid(p.tgid) as u64,
                                    p.linux.vfs.visible_pid(p.parent) as u64,
                                    c.uid as u64,
                                    c.euid as u64,
                                    c.gid as u64,
                                    c.egid as u64,
                                    p.linux.vfs.visible_pid(id) as u64,
                                ],
                                started: self.started,
                                processor: slot as u32,
                                profile: self.profile_syscalls,
                                streams: p.linux.vfs.stream_descriptors(),
                                files: p.linux.vfs.file_descriptors(),
                                fd_flags: p.linux.vfs.fd_flags_snapshot(),
                            }
                        }),
                    },
                )
                .map_err(|_| RuntimeError::Arguments("CPU worker disconnected"))?;
        }
        self.parallel_dispatch_time += began.elapsed();
        let mut completed = Vec::new();
        let deadline = Instant::now() + timeout;
        let mut disk = DiskEpoch::default();
        loop {
            let left = deadline.saturating_duration_since(Instant::now());
            if left.is_zero() {
                break;
            }
            match self
                .workers
                .event_timeout(left)
                .map_err(|_| RuntimeError::Arguments("CPU worker failed"))?
            {
                Some(crate::workers::Event::Done(done)) => {
                    completed.push(*done);
                    break;
                }
                Some(crate::workers::Event::Service(request)) => {
                    self.service_requests += 1;
                    if !self.epoch_request(request, &mut disk, io) {
                        break;
                    }
                }
                Some(crate::workers::Event::DiskRead(id, result)) => {
                    self.finish_disk_read(id, result, &mut disk);
                    if !self.resume_disk_waiters(&mut disk, io) {
                        break;
                    }
                }
                None => break,
            }
        }
        control.stop();
        let began = Instant::now();
        // An already submitted read must finish/commit exactly once before
        // mapping, process or FD mutation. Do not wait in Pool::complete while
        // its caller is parked awaiting a disk completion on this same channel.
        for request in disk.waiting.drain(..) {
            request.respond(None);
        }
        while completed.len() < chosen.len() || !disk.pending.is_empty() {
            match self
                .workers
                .event_timeout(Duration::from_secs(1))
                .map_err(|_| RuntimeError::Arguments("CPU worker failed"))?
            {
                Some(crate::workers::Event::Done(done)) => completed.push(*done),
                Some(crate::workers::Event::Service(request)) => request.respond(None),
                Some(crate::workers::Event::DiskRead(id, result)) => {
                    self.finish_disk_read(id, result, &mut disk);
                }
                None => {}
            }
        }
        self.parallel_stop_time += began.elapsed();
        if completed.iter().any(|r| r.outcome.is_none()) {
            for done in completed {
                self.processes
                    .get_mut(&done.pid)
                    .expect("dispatched process")
                    .cpu = done.cpu;
            }
            return Err(RuntimeError::Arguments("CPU worker panicked"));
        }
        // First restore *all* CPUs and bytes, then handle exits/exec/fork, which
        // may remove peers. This also makes FUTEX_WAIT compare+enqueue indivisible
        // with respect to guest stores and all FUTEX_WAKE operations.
        let began = Instant::now();
        for (index, &(id, _)) in chosen.iter().enumerate() {
            let vm_id = self.processes[&id].vm_id;
            if chosen[..index]
                .iter()
                .any(|&(peer, _)| self.processes[&peer].vm_id == vm_id)
            {
                continue;
            }
            self.processes
                .get_mut(&id)
                .expect("dispatched process")
                .memory
                .commit_parallel()?;
        }
        self.parallel_memory_time += began.elapsed();
        let mut outcomes = Vec::new();
        for done in completed {
            self.native.metrics.merge(done.native);
            let p = self
                .processes
                .get_mut(&done.pid)
                .expect("dispatched process");
            p.cpu = done.cpu;
            if let Some((pc, module, function)) = done.rejected_native {
                p.linux.vfs.log(4,&format!("pid={} NativeCall rejected module={module:#x} function={function} pc={pc:#x} comm={:?}",done.pid,String::from_utf8_lossy(&p.linux.command_name)));
            }
            p.last_cpu = done.worker;
            p.cpu_time += done.elapsed;
            self.cpu_total += done.elapsed;
            self.per_cpu[done.worker] += done.elapsed;
            self.decode_lookups += done.after.lookups - done.before.lookups;
            self.decode_misses += done.after.misses - done.before.misses;
            self.instructions_decoded += done.after.instructions - done.before.instructions;
            let outcome = done.outcome.expect("checked worker completion");
            self.instructions += done.instructions;
            self.worker_chunks += done.chunks;
            self.worker_syscalls += done.syscalls;
            self.serviced_syscalls += done.serviced_syscalls;
            self.service_wait += done.service_wait;
            self.syscalls += done.syscalls + done.serviced_syscalls;
            self.syscall_total += done.syscall_time;
            outcomes.push((done.pid, outcome));
        }
        self.parallel_rounds += 1;
        self.worker_slices += chosen.len() as u64;
        self.context_switches += chosen.len() as u64;
        self.next_parallel_pid = chosen.last().expect("two workers").0;
        outcomes.sort_by_key(|v| v.0);
        for (id, outcome) in outcomes {
            let Some(mut p) = self.processes.remove(&id) else {
                continue;
            };
            // A preceding exit_group/exec may have killed this returned peer.
            if !matches!(p.state, State::Zombie(_)) {
                // KILL queued by a preceding cgroup.kill cannot permit an
                // uncommitted fork to create a survivor outside its snapshot.
                if p.pending_bits() & (1 << 8) != 0 {
                    let _ = self.deliver(id, &mut p);
                } else {
                    self.finish_slice(id, &mut p, outcome, io, false);
                }
            }
            let actual = p.rekey.take().unwrap_or(id);
            if !p.auto_reap || actual == 1 {
                self.processes.insert(actual, p);
            }
        }
        Ok(true)
    }
    fn parallel_timeout(&self) -> Duration {
        let now = Instant::now();
        // Bounded host control and affinity fairness even when every guest spins
        // forever. A local instruction poll does not yield to the coordinator.
        let mut timeout = Duration::from_millis(10);
        if self.terminal.is_some() {
            timeout = Duration::from_millis(2);
        }
        for timer in self.timers.values() {
            if let Some(deadline) = timer.deadline {
                timeout = timeout.min(deadline.saturating_duration_since(now));
            }
        }
        for (&id, p) in &self.processes {
            if p.frozen(id) {
                continue;
            }
            if matches!(p.state, State::Gpu { .. }) {
                timeout = timeout.min(Duration::from_millis(1));
            }
            // An unread expired timerfd remains readable until a guest read or
            // poll updates it. It must not force zero-length CPU epochs forever.
            let timerfd = p.linux.vfs.next_timerfd_deadline().filter(|d| *d > now);
            // A failed/restarted poll can leave a saved deadline on a runnable
            // or stopped task. Only an active blocked poll needs a wakeup poll.
            let poll = matches!(p.state, State::Blocked(22 | 72 | 73 | 441, _))
                .then_some(p.poll_deadline)
                .flatten();
            let socket = matches!(p.state, State::Blocked(..))
                .then_some(p.socket_deadline)
                .flatten();
            for deadline in [poll, timerfd, socket].into_iter().flatten() {
                timeout = timeout.min(deadline.saturating_duration_since(now));
            }
            if let State::Futex {
                deadline: Some(d), ..
            }
            | State::Sigwait {
                deadline: Some(d), ..
            }
            | State::Sleep(d, _) = p.state
            {
                timeout = timeout.min(d.saturating_duration_since(now));
            }
            if matches!(p.state, State::Blocked(..)) && p.linux.vfs.live_sockets() != 0 {
                timeout = timeout.min(Duration::from_millis(1));
            }
        }
        timeout
    }
    fn serial_quantum(&self) -> u64 {
        // No runnable peer needs a turn. Keep a bounded CPU slice while
        // amortizing control/PTY/timer and blocked-task checks. A syscall or
        // native callback outcome still returns through finish_slice normally.
        if self
            .processes
            .iter()
            .any(|(&id, p)| matches!(p.state, State::Ready) && !p.frozen(id))
        {
            4096
        } else {
            65536
        }
    }
    fn drive(&mut self, io: &mut impl GuestOutput, limit: u64) -> Result<Exit, RuntimeError> {
        loop {
            self.pump_guest_ptys();
            self.tick_timers();
            let elapsed = self.load_sample.elapsed().as_secs_f64();
            if elapsed >= 5.0 {
                let running = self
                    .processes
                    .values()
                    .filter(|p| matches!(p.state, State::Ready))
                    .count() as f64;
                for (load, seconds) in self.load.iter_mut().zip([60.0, 300.0, 900.0]) {
                    let decay = (-elapsed / seconds).exp();
                    *load = *load * decay + running * (1.0 - decay);
                }
                self.load_sample = Instant::now();
            }
            if let Some(root) = self.processes.get_mut(&1) {
                match io.control(&mut root.linux.vfs) {
                    Ok(true) => {}
                    Ok(false) => return Err(RuntimeError::Cancelled),
                    Err(_) => return Err(RuntimeError::Arguments("host session control failed")),
                }
            }
            for backend in self.gpu.borrow_mut().values_mut() {
                backend.reap_abandoned();
            }
            let mut progress = self.pump_terminal(io)?;
            if let Some(root) = self.processes.get(&1) {
                progress |= root.linux.vfs.pump_native_services();
            }
            if self.parallel_round(io, limit)? {
                continue;
            }
            let ids: Vec<u32> = self.processes.keys().copied().collect();
            for id in ids {
                let Some(mut p) = self.processes.remove(&id) else {
                    continue;
                };
                if !matches!(p.state, State::Zombie(_)) && !p.frozen(id) {
                    progress |= self.resume_gpu(id, &mut p);
                    progress |= self.resume_event(&mut p);
                    match self.deliver(id, &mut p) {
                        Ok(changed) => progress |= changed,
                        Err(_) => {
                            self.exit(id, &mut p, 11);
                            progress = true;
                        }
                    }
                    self.sync_resources(&p);
                    match p.state.clone() {
                        State::Blocked(number, a) => {
                            self.call(id, &mut p, number, a, io);
                            progress |= !matches!(p.state, State::Blocked(..));
                        }
                        State::Sleep(deadline, _) if Instant::now() >= deadline => {
                            p.state = State::Ready;
                            p.cpu.set_x(0, 0);
                            progress = true;
                        }
                        _ => {}
                    }
                    // Immediate pure/callback returns keep the current quantum.
                    // No native borrow survives; blocking syscalls/faults still
                    // return to the scheduler. Bound work/time between control,
                    // signal and timer polls independently of guest retirement.
                    let mut quantum = if matches!(p.state, State::Ready) {
                        self.serial_quantum()
                    } else {
                        4096
                    };
                    let quantum_started = Instant::now();
                    let mut native_returns = 0;
                    while matches!(p.state, State::Ready) && quantum != 0 {
                        if self.instructions >= limit {
                            self.processes.insert(id, p);
                            return Err(RuntimeError::BudgetExceeded(limit));
                        }
                        let instructions_before = self.instructions;
                        let began = Instant::now();
                        let decode_before = p.cpu.decode_stats();
                        let outcome = p
                            .cpu
                            .run_slice(&mut p.memory, (limit - self.instructions).min(quantum));
                        let elapsed = began.elapsed();
                        let decode_after = p.cpu.decode_stats();
                        self.decode_lookups += decode_after.lookups - decode_before.lookups;
                        self.decode_misses += decode_after.misses - decode_before.misses;
                        self.instructions_decoded +=
                            decode_after.instructions - decode_before.instructions;
                        p.cpu_time += elapsed;
                        self.cpu_total += elapsed;
                        p.last_cpu = p.affinity.trailing_zeros() as usize;
                        self.per_cpu[p.last_cpu] += elapsed;
                        self.context_switches += 1;
                        let immediate_return = self.finish_slice(id, &mut p, outcome, io, true);
                        if immediate_return {
                            native_returns += 1;
                        }
                        progress = true;
                        quantum = quantum.saturating_sub(self.instructions - instructions_before);
                        if !immediate_return
                            || native_returns >= 32
                            || quantum_started.elapsed() >= Duration::from_millis(2)
                        {
                            break;
                        }
                    }
                }
                let actual_id = p.rekey.take().unwrap_or(id);
                if !p.auto_reap || actual_id == 1 {
                    self.processes.insert(actual_id, p);
                }
                let root_status = self.processes.get(&1).and_then(|root| {
                    if let State::Zombie(s) = root.state {
                        Some(s)
                    } else {
                        None
                    }
                });
                if let Some(status) = root_status {
                    if !self
                        .processes
                        .values()
                        .any(|t| t.tgid == 1 && !matches!(t.state, State::Zombie(_)))
                    {
                        // Session teardown is explicit: root exit cancels remaining virtual children.
                        if let Some(root) = self.processes.get(&1) {
                            root.linux.vfs.finish_network();
                        }
                        while self
                            .terminal
                            .as_ref()
                            .is_some_and(|t| !t.output_queue.is_empty())
                        {
                            if let Some(root) = self.processes.get_mut(&1) {
                                if !io.control(&mut root.linux.vfs).map_err(|_| {
                                    RuntimeError::Arguments("host session control failed")
                                })? {
                                    return Err(RuntimeError::Cancelled);
                                }
                            }
                            if !self.pump_terminal(io)? {
                                // A native renderer can legitimately be full.
                                // Preserve final output until it drains or the
                                // embedding app explicitly cancels the session.
                                self.wait_external();
                            }
                        }
                        return Ok(Exit {
                            code: if status & 127 == 0 {
                                (status >> 8) as u8
                            } else {
                                128 + (status & 127) as u8
                            },
                            instructions: self.instructions,
                        });
                    }
                }
            }
            if !progress {
                let external = self.processes.values().any(|p| {
                    p.poll_deadline.is_some()
                        || matches!(p.state, State::Gpu { .. })
                        || p.linux.vfs.next_timerfd_deadline().is_some()
                        || matches!(p.state, State::Futex { deadline: Some(_), .. } | State::Sigwait { deadline: Some(_), .. })
                        || matches!(p.state, State::Sleep(..))
                        || matches!(p.state, State::Blocked(63 | 65, a)
                        if p.linux.vfs.fd_kind(a[0] as u32).ok() == Some(FdKind::Stdin))
                        || matches!(p.state, State::Blocked(number, a)
                        if matches!(number, 63 | 64 | 65 | 66 | 202 | 203 | 242 | 206 | 207 | 211 | 212)
                        && p.linux.vfs.fd_kind(a[0] as u32).ok() == Some(FdKind::Socket))
                        || matches!(p.state,State::Blocked(76,a)if [a[0],a[2]].iter().any(|fd|p.linux.vfs.fd_kind(*fd as u32).ok()==Some(FdKind::Socket)))
                        || (matches!(p.state, State::Blocked(22 | 72 | 73 | 441, _))
                            && p.linux.vfs.live_sockets() != 0)
                        || p.linux.vfs.has_bridge_descriptors()
                        || p.linux.vfs.has_native_services()
                }) || self.terminal.is_some() || self.timers.values().any(|t| t.deadline.is_some());
                if !external {
                    return Err(RuntimeError::Deadlock);
                }
                self.wait_external();
            }
        }
    }
    fn wait_external(&mut self) {
        let began = Instant::now();
        let mut sockets = Vec::new();
        let now = Instant::now();
        let mut timeout = Duration::from_millis(20);
        if self.terminal.is_some() {
            timeout = Duration::from_millis(1);
        }
        for timer in self.timers.values() {
            if let Some(d) = timer.deadline {
                timeout = timeout.min(d.saturating_duration_since(now));
            }
        }
        for (&id, p) in &self.processes {
            if p.frozen(id) {
                continue;
            }
            if matches!(p.state, State::Gpu { .. }) {
                timeout = timeout.min(Duration::from_millis(1));
            }
            if let Some(deadline) = p.linux.vfs.next_timerfd_deadline() {
                timeout = timeout.min(deadline.saturating_duration_since(now));
            }
            if let State::Futex {
                deadline: Some(d), ..
            }
            | State::Sigwait {
                deadline: Some(d), ..
            } = p.state
            {
                timeout = timeout.min(d.saturating_duration_since(now));
            }
            if let Some(deadline) = p.poll_deadline {
                timeout = timeout.min(deadline.saturating_duration_since(now));
            }
            if matches!(p.state, State::Blocked(..)) {
                if let Some(deadline) = p.socket_deadline {
                    timeout = timeout.min(deadline.saturating_duration_since(now));
                }
            }
            if let State::Sleep(deadline, _) = p.state {
                timeout = timeout.min(deadline.saturating_duration_since(now));
            }
            let State::Blocked(number, a) = p.state else {
                continue;
            };
            let mut add = |fd, events| {
                if let Ok(socket) = p.linux.vfs.socket_fd(fd) {
                    sockets.push((socket, events));
                }
            };
            match number {
                63 | 65 | 202 | 242 | 207 | 212 => {
                    add(a[0] as u32, 1);
                    if p.linux.vfs.fd_kind(a[0] as u32).ok() == Some(FdKind::Stdin) {
                        timeout = timeout.min(Duration::from_millis(1));
                    }
                }
                76 => {
                    add(a[0] as u32, 1);
                    add(a[2] as u32, 4);
                }
                64 | 66 | 203 | 206 | 211 => add(a[0] as u32, 4),
                73 => {
                    for i in 0..a[1].min(256) {
                        if let (Ok(fd), Ok(events)) = (
                            read_u32(&p.memory, a[0] + i * 8),
                            read_u32(&p.memory, a[0] + i * 8 + 4),
                        ) {
                            if fd < i32::MAX as u32 {
                                add(fd, events as u16);
                            }
                            if p.linux.vfs.fd_kind(fd).ok() == Some(FdKind::Stdin) {
                                timeout = timeout.min(Duration::from_millis(1));
                            }
                        }
                    }
                }
                72 => {
                    for (index, event) in [(1, 1), (2, 4), (3, 2)] {
                        if a[index] == 0 {
                            continue;
                        }
                        for fd in 0..a[0].min(256) as u32 {
                            if let Ok(bits) = read_u64(&p.memory, a[index] + (fd as u64 / 64) * 8) {
                                if bits & (1 << (fd % 64)) != 0 {
                                    add(fd, event);
                                    if p.linux.vfs.fd_kind(fd).ok() == Some(FdKind::Stdin) {
                                        timeout = timeout.min(Duration::from_millis(1));
                                    }
                                }
                            }
                        }
                    }
                }
                _ => {}
            }
        }
        if sockets.is_empty() || timeout < Duration::from_millis(1) {
            std::thread::sleep(timeout.min(Duration::from_millis(1)));
        } else if trames_net::wait(&sockets, timeout).is_err() {
            // Native adapter failures are surfaced by the next guest IO/poll.
            std::thread::sleep(Duration::from_millis(1));
        }
        self.idle += began.elapsed();
    }
}
pub(super) fn run(
    runtime: &mut Runtime,
    io: &mut impl GuestOutput,
    limit: u64,
) -> Result<Exit, RuntimeError> {
    runtime.memory_limits.validate()?;
    runtime
        .memory
        .set_limit(runtime.memory_limits.per_process)?;
    let mut root = Process {
        cpu: std::mem::take(&mut runtime.cpu),
        affinity: (1u64 << runtime.cpu_count.clamp(1, 32)) - 1,
        last_cpu: 0,
        nice: 0,
        scheduler: 0,
        memory: std::mem::take(&mut runtime.memory),
        linux: std::mem::replace(&mut runtime.linux, Linux::new(0)),
        parent: 0,
        parent_thread: 0,
        pdeath_signal: 0,
        group: 1,
        session: 1,
        state: State::Ready,
        actions: [Action::default(); 65],
        mask: 0,
        pending: Pending::default(),
        shared_pending: Rc::new(RefCell::new(Pending::default())),
        frames: 0,
        wait_event: None,
        poll_deadline: None,
        socket_deadline: None,
        poll_mask: None,
        pending_open: None,
        stopped_state: None,
        auto_reap: false,
        tgid: 1,
        started: Instant::now(),
        cpu_time: Duration::ZERO,
        vm_id: 1,
        files_id: 1,
        fs_id: 1,
        actions_id: 1,
        rekey: None,
        subreaper: false,
        alt_stack: AltStack::default(),
        membarrier_registered: Rc::new(Cell::new(false)),
        file_size_limit: Rc::new(Cell::new((u64::MAX, u64::MAX))),
        oom: Rc::new(trames_vfs::OomTask::default()),
        seccomp: seccomp::Seccomp::default(),
    };
    let count = runtime.cpu_count.clamp(1, 32);
    let mut kernel = Kernel {
        memory_limits: runtime.memory_limits,
        cpu_count: count,
        workers: crate::workers::Pool::with_profiling(count, runtime.profile_syscalls),
        parallel_memory: trames_memory::parallel::Registry::default(),
        parallel_rounds: 0,
        worker_slices: 0,
        worker_chunks: 0,
        worker_syscalls: 0,
        service_requests: 0,
        disk_reads: 0,
        disk_read_bytes: 0,
        service_write_buffer: Vec::new(),
        serviced_syscalls: 0,
        service_wait: Duration::ZERO,
        parallel_memory_time: Duration::ZERO,
        parallel_dispatch_time: Duration::ZERO,
        parallel_stop_time: Duration::ZERO,
        next_parallel_pid: 0,
        per_cpu: vec![Duration::ZERO; count],
        processes: BTreeMap::new(),
        next_pid: 2,
        terminal: runtime.terminal.take(),
        started: Instant::now(),
        instructions: 0,
        boot_time: std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs(),
        context_switches: 0,
        decode_lookups: 0,
        instructions_decoded: 0,
        decode_misses: 0,
        cpu_total: Duration::ZERO,
        syscall_total: Duration::ZERO,
        syscalls: 0,
        proc_refreshes: 0,
        proc_maps: RefCell::new(BTreeMap::new()),
        profile_syscalls: runtime.profile_syscalls,
        idle: Duration::ZERO,
        load: [0.0; 3],
        load_sample: Instant::now(),
        next_resource: 2,
        timers: BTreeMap::new(),
        next_timer: 1,
        native: trames_accel::native::Registry::default(),
        gpu: RefCell::new(BTreeMap::new()),
        gpu_budget: trames_vulkan::Budget::default(),
        gpu_display: runtime.vulkan_display.clone(),
        gpu_compiles: 0,
        gpu_peak_pending: 0,
        gpu_peer_instructions: 0,
        gpu_wait_time: Duration::ZERO,
        gpu_transfers: trames_vulkan::TransferMetrics::default(),
    };
    // Enable all virtual mounts once, including /dev and /sys for programs
    // that never touch /proc. Subsequent full snapshots are demand-driven.
    kernel.refresh_proc(1, &mut root);
    kernel.proc_refreshes += 1;
    kernel.processes.insert(1, root);
    let result = kernel.drive(io, limit);
    runtime.metrics = super::RuntimeMetrics {
        instructions: kernel.instructions,
        parallel_snapshots: kernel.parallel_memory.metrics,
        cpu_count: kernel.cpu_count,
        parallel_rounds: kernel.parallel_rounds,
        worker_slices: kernel.worker_slices,
        worker_chunks: kernel.worker_chunks,
        worker_syscalls: kernel.worker_syscalls,
        service_requests: kernel.service_requests,
        disk_reads: kernel.disk_reads,
        disk_read_bytes: kernel.disk_read_bytes,
        serviced_syscalls: kernel.serviced_syscalls,
        service_wait_time: kernel.service_wait,
        peak_parallel_native_calls: kernel
            .workers
            .peak_native
            .load(std::sync::atomic::Ordering::Relaxed),
        peak_parallel_cpus: kernel
            .workers
            .peak
            .load(std::sync::atomic::Ordering::Relaxed),
        parallel_memory_time: kernel.parallel_memory_time,
        parallel_dispatch_time: kernel.parallel_dispatch_time,
        parallel_stop_time: kernel.parallel_stop_time,
        cpu_time: kernel.cpu_total,
        syscall_time: kernel.syscall_total,
        idle_time: kernel.idle,
        syscalls: kernel.syscalls,
        proc_refreshes: kernel.proc_refreshes,
        context_switches: kernel.context_switches,
        decode_lookups: kernel.decode_lookups,
        instructions_decoded: kernel.instructions_decoded,
        decode_misses: kernel.decode_misses,
        native: kernel.native.metrics,
        gpu_compiles: kernel.gpu_compiles,
        gpu_peak_pending: kernel.gpu_peak_pending,
        gpu_peer_instructions: kernel.gpu_peer_instructions,
        gpu_wait_time: kernel.gpu_wait_time,
        gpu_transfers: kernel.gpu_transfers,
    };
    // Session teardown cancels forked children, including their process locks.
    // Root-thread locks are already released on normal group exit; preserve
    // the root's state when returning a resumable runtime error.
    for p in kernel.processes.values_mut().filter(|p| p.tgid != 1) {
        p.linux.vfs.set_lock_owner(p.tgid);
        p.linux.vfs.release_process_locks();
    }
    if let Some(p) = kernel.processes.remove(&1) {
        runtime.cpu = p.cpu;
        runtime.memory = p.memory;
        runtime.linux = p.linux;
    }
    runtime.terminal = kernel.terminal;
    result
}

#[cfg(test)]
mod tests {
    use super::*;
    use trames_memory::{Permissions, PAGE_SIZE};
    #[test]
    fn solo_quantum_does_not_delay_a_runnable_peer() {
        let mut k = kernel();
        assert_eq!(k.serial_quantum(), 65536);
        let mut blocked = process();
        blocked.state = State::Blocked(260, [0; 6]);
        k.processes.insert(2, blocked);
        assert_eq!(k.serial_quantum(), 65536);
        k.processes.get_mut(&2).unwrap().state = State::Ready;
        assert_eq!(k.serial_quantum(), 4096);
    }
    #[test]
    fn affinity_assignment_keeps_flexible_tasks_fair_and_relocates_for_pinned_peers() {
        assert_eq!(
            assign_cpus([(1, 3), (2, 1), (3, 2)], 2),
            vec![(1, 1), (2, 0)]
        );
        assert_eq!(
            assign_cpus([(3, 2), (1, 3), (2, 1)], 2),
            vec![(3, 1), (1, 0)]
        );
        assert_eq!(
            assign_cpus([(1, 1), (2, 1), (3, 2)], 2),
            vec![(1, 0), (3, 1)]
        );
        assert_eq!(assign_cpus([(1, 3), (2, 3)], 2), vec![(1, 0), (2, 1)]);
    }
    pub(super) fn process() -> Process {
        let mut memory = Memory::default();
        memory.map(0x1000, 8 * PAGE_SIZE, Permissions::RW).unwrap();
        Process {
            cpu: Cpu::new(0x4000, 0x9000),
            affinity: 1,
            last_cpu: 0,
            nice: 0,
            scheduler: 0,
            memory,
            linux: Linux::new(0xa000),
            parent: 0,
            parent_thread: 0,
            pdeath_signal: 0,
            group: 1,
            session: 1,
            state: State::Ready,
            actions: [Action::default(); 65],
            mask: 0,
            pending: Pending::default(),
            shared_pending: Rc::new(RefCell::new(Pending::default())),
            frames: 0,
            wait_event: None,
            poll_deadline: None,
            socket_deadline: None,
            poll_mask: None,
            pending_open: None,
            stopped_state: None,
            auto_reap: false,
            tgid: 1,
            started: Instant::now(),
            cpu_time: Duration::ZERO,
            vm_id: 1,
            files_id: 1,
            fs_id: 1,
            actions_id: 1,
            rekey: None,
            subreaper: false,
            alt_stack: AltStack::default(),
            membarrier_registered: Rc::new(Cell::new(false)),
            file_size_limit: Rc::new(Cell::new((u64::MAX, u64::MAX))),
            oom: Rc::new(trames_vfs::OomTask::default()),
            seccomp: seccomp::Seccomp::default(),
        }
    }
    #[test]
    fn terminal_control_signals_use_kernel_authority_and_the_foreground_session() {
        let mut k = kernel();
        let mut sender = process();
        sender.linux.vfs.credentials = trames_vfs::Credentials::user(1000, 1000);
        let mut root_command = process();
        root_command.session = 9;
        root_command.group = 3;
        assert!(!Kernel::may_signal(&sender, &root_command, 2));
        let mut another_session = root_command.fork();
        another_session.session = 10;
        k.processes.insert(3, root_command);
        k.processes.insert(4, another_session);
        k.terminal_signal(1, &mut sender, 9, 3, 2);
        assert!(k.processes[&3].pending.contains_key(&2));
        assert!(!k.processes[&4].pending.contains_key(&2));
        assert!(!sender.pending.contains_key(&2));
    }
    #[test]
    fn host_pty_keyboard_and_resize_signals_are_scoped_to_the_foreground_session() {
        let mut k = kernel();
        let mut root = process();
        k.refresh_proc(1, &mut root);
        let fd = root.linux.vfs.open_at(-100, b"/dev/ptmx", 2, 0).unwrap();
        let pair = root.linux.vfs.pty(fd).unwrap().unwrap().pair.clone();
        {
            let mut tty = pair.terminal.borrow_mut();
            tty.session = 9;
            tty.foreground = 3;
        }
        pair.resize_from_host(40, 120);
        pair.host_signals.set(1 << 1);
        let mut foreground = process();
        foreground.session = 9;
        foreground.group = 3;
        let mut other = foreground.fork();
        other.session = 10;
        let mut background = foreground.fork();
        background.group = 4;
        k.processes.insert(1, root);
        k.processes.insert(3, foreground);
        k.processes.insert(4, other);
        k.processes.insert(5, background);
        k.pump_guest_ptys();
        assert!(k.processes[&3].pending.contains_key(&2));
        assert!(k.processes[&3].pending.contains_key(&28));
        for id in [1, 4, 5] {
            assert!(!k.processes[&id].pending.contains_key(&2));
            assert!(!k.processes[&id].pending.contains_key(&28));
        }
        assert_eq!(pair.host_signals.get(), 0);
        assert!(!pair.host_resize_pending.get());
    }
    pub(super) fn kernel() -> Kernel {
        Kernel {
            memory_limits: super::super::MemoryLimits::default(),
            cpu_count: 1,
            workers: crate::workers::Pool::new(1),
            parallel_memory: trames_memory::parallel::Registry::default(),
            parallel_rounds: 0,
            worker_slices: 0,
            worker_chunks: 0,
            worker_syscalls: 0,
            service_requests: 0,
            disk_reads: 0,
            disk_read_bytes: 0,
            service_write_buffer: Vec::new(),
            serviced_syscalls: 0,
            service_wait: Duration::ZERO,
            parallel_memory_time: Duration::ZERO,
            parallel_dispatch_time: Duration::ZERO,
            parallel_stop_time: Duration::ZERO,
            next_parallel_pid: 0,
            per_cpu: vec![Duration::ZERO; 1],
            processes: BTreeMap::new(),
            next_pid: 2,
            terminal: None,
            started: Instant::now(),
            instructions: 0,
            boot_time: std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap_or_default()
                .as_secs(),
            context_switches: 0,
            decode_lookups: 0,
            instructions_decoded: 0,
            decode_misses: 0,
            cpu_total: Duration::ZERO,
            syscall_total: Duration::ZERO,
            syscalls: 0,
            proc_refreshes: 0,
            proc_maps: RefCell::new(BTreeMap::new()),
            profile_syscalls: false,
            idle: Duration::ZERO,
            load: [0.0; 3],
            load_sample: Instant::now(),
            next_resource: 2,
            timers: BTreeMap::new(),
            next_timer: 1,
            native: trames_accel::native::Registry::default(),
            gpu: RefCell::new(BTreeMap::new()),
            gpu_budget: trames_vulkan::Budget::default(),
            gpu_display: trames_vulkan::Display::default(),
            gpu_compiles: 0,
            gpu_peak_pending: 0,
            gpu_peer_instructions: 0,
            gpu_wait_time: Duration::ZERO,
            gpu_transfers: trames_vulkan::TransferMetrics::default(),
        }
    }
    #[test]
    fn proc_maps_refresh_after_shared_protect_reserve_unmap_and_fork() {
        let k = kernel();
        let mut p = process();
        assert_eq!(
            k.process_maps(&p),
            b"000000001000-000000009000 rw-p 00000000 00:00 0\n"
        );
        let mut peer = p.fork();
        peer.memory = p.memory.share();
        peer.memory
            .protect(0x2000, PAGE_SIZE, Permissions::R)
            .unwrap();
        let maps = k.process_maps(&p);
        assert!(String::from_utf8_lossy(&maps).contains("000000002000-000000003000 r--p"));
        assert_eq!(k.process_maps(&peer), maps);
        p.memory.reserve(0x100000, 2 * PAGE_SIZE, false).unwrap();
        let maps = String::from_utf8(k.process_maps(&p)).unwrap();
        assert!(maps.contains("000000100000-000000102000 ---p"));
        p.memory
            .protect(0x100000, PAGE_SIZE, Permissions::RW)
            .unwrap();
        let maps = String::from_utf8(k.process_maps(&peer)).unwrap();
        assert!(maps.contains("000000100000-000000101000 rw-p"));
        p.memory.unmap(0x100000, 2 * PAGE_SIZE).unwrap();
        assert!(!String::from_utf8(k.process_maps(&p))
            .unwrap()
            .contains("000000100000-"));
        let mut child = p.fork();
        child.vm_id = 2;
        child
            .memory
            .protect(0x2000, PAGE_SIZE, Permissions::RW)
            .unwrap();
        assert_eq!(
            k.process_maps(&child),
            b"000000001000-000000009000 rw-p 00000000 00:00 0\n"
        );
        assert_eq!(k.process_maps(&p), k.process_maps(&peer));
        assert_ne!(k.process_maps(&p), k.process_maps(&child));
        // An exited group leader has released its Memory while surviving
        // threads still share the old vm_id. It must not evict their maps.
        let mut leader = process();
        leader.vm_id = p.vm_id;
        leader.memory = Memory::default();
        assert!(k.process_maps(&leader).is_empty());
        assert_eq!(k.process_maps(&p), k.process_maps(&peer));
        assert_eq!(k.proc_maps.borrow().len(), 3);
    }

    #[test]
    fn proc_mapping_cache_never_freezes_argv_fds_or_credentials_and_retires_dead_vms() {
        let k = kernel();
        let mut p = process();
        p.linux.arguments = vec![b"first".to_vec()];
        p.linux.argument_pointers = vec![0x1000];
        p.memory.write(0x1000, b"first\0").unwrap();
        let before = k.process_info(1, &p);
        p.memory.write(0x1000, b"other\0").unwrap();
        let pipe = p.linux.vfs.pipe(0).unwrap();
        p.linux.vfs.credentials = trames_vfs::Credentials::user(1000, 1000);
        let after = k.process_info(1, &p);
        assert_eq!(before.maps, after.maps);
        assert_eq!(before.arguments, [b"first".to_vec()]);
        assert_eq!(after.arguments, [b"other".to_vec()]);
        assert_eq!(after.credentials.euid, 1000);
        assert_eq!(after.fds.len(), before.fds.len() + 2);
        assert!(after.fds.iter().any(|fd| fd.fd == pipe[0]));
        let mut old = p.fork();
        old.vm_id = 2;
        k.process_maps(&old);
        assert_eq!(k.proc_maps.borrow().len(), 2);
        k.refresh_proc(1, &mut p);
        assert_eq!(k.proc_maps.borrow().len(), 1);
    }

    #[test]
    fn native_workers_execute_shared_atomic_guest_loops_and_account_the_budget() {
        let mut p = process();
        p.memory = Memory::default();
        p.memory
            .map(
                0x4000,
                trames_memory::PAGE_SIZE,
                trames_memory::Permissions::RW,
            )
            .unwrap();
        p.memory
            .map(
                0x8000,
                trames_memory::PAGE_SIZE,
                trames_memory::Permissions::RW,
            )
            .unwrap();
        // ldaddal x1,x3,[x0]; subs x2,x2,#1; b.ne -8; exit(0).
        let code = [
            0xf8e10003u32,
            0xf1000442,
            0x54ffffc1,
            0xd2800ba8,
            0xd2800000,
            0xd4000001,
        ];
        for (i, word) in code.iter().enumerate() {
            p.memory
                .write(0x4000 + i as u64 * 4, &word.to_le_bytes())
                .unwrap();
        }
        p.memory
            .protect(
                0x4000,
                trames_memory::PAGE_SIZE,
                trames_memory::Permissions::RX,
            )
            .unwrap();
        p.cpu = Cpu::new(0x4000, 0);
        p.cpu.set_x(0, 0x8000);
        p.cpu.set_x(1, 1);
        p.cpu.set_x(2, 50000);
        p.affinity = 3;
        let mut peer = p.fork();
        peer.memory = p.memory.share();
        peer.tgid = 1;
        peer.parent = 1;
        peer.auto_reap = false;
        let mut k = kernel();
        k.cpu_count = 2;
        k.workers = crate::workers::Pool::new(2);
        k.per_cpu = vec![Duration::ZERO; 2];
        k.next_pid = 3;
        k.processes.insert(1, p);
        k.processes.insert(2, peer);
        let exit = k
            .drive(&mut trames_linux::CapturedOutput::default(), 400000)
            .unwrap();
        assert_eq!(exit.code, 0);
        assert_eq!(k.processes[&1].memory.read_u64(0x8000).unwrap(), 100000);
        assert_eq!(exit.instructions, 300006);
        assert!(k.parallel_rounds > 0);
        // Without a startup barrier the host may schedule these finite tasks
        // consecutively. Real SMP publication fixtures require overlapping work.
        assert!(k.workers.peak.load(std::sync::atomic::Ordering::Relaxed) > 0);
    }
    fn spinning_peers() -> Kernel {
        let mut p = process();
        // add x0,x0,#1; b -4. No guest syscall can bring these CPUs back.
        for (i, word) in [0x91000400u32, 0x17ffffff].into_iter().enumerate() {
            p.memory
                .write(0x4000 + i as u64 * 4, &word.to_le_bytes())
                .unwrap();
        }
        p.memory
            .protect(
                0x4000,
                trames_memory::PAGE_SIZE,
                trames_memory::Permissions::RX,
            )
            .unwrap();
        p.affinity = 3;
        let mut peer = p.fork();
        peer.memory = p.memory.share();
        peer.parent = 1;
        let mut k = kernel();
        k.cpu_count = 2;
        k.workers = crate::workers::Pool::new(2);
        k.per_cpu = vec![Duration::ZERO; 2];
        k.processes.insert(1, p);
        k.processes.insert(2, peer);
        k
    }
    #[test]
    fn continuous_peers_cancel_at_control_checkpoints_and_commit_the_exact_budget() {
        struct CancelAfterTwoPolls(usize);
        impl GuestOutput for CancelAfterTwoPolls {
            fn control(&mut self, _: &mut trames_vfs::Vfs) -> std::io::Result<bool> {
                self.0 += 1;
                Ok(self.0 < 3)
            }
            fn write(&mut self, _: OutputFd, bytes: &[u8]) -> std::io::Result<usize> {
                Ok(bytes.len())
            }
        }
        let mut k = spinning_peers();
        assert!(matches!(
            k.drive(&mut CancelAfterTwoPolls(0), u64::MAX),
            Err(RuntimeError::Cancelled)
        ));
        assert!(k.instructions > 0);
        assert!(k.worker_chunks > 0);
        for limit in [1, 17, 65_539] {
            let mut k = spinning_peers();
            assert!(
                matches!(k.drive(&mut trames_linux::CapturedOutput::default(), limit), Err(RuntimeError::BudgetExceeded(n)) if n == limit)
            );
            assert_eq!(k.instructions, limit);
            assert_eq!(
                k.processes.values().map(|p| p.cpu.x(0)).sum::<u64>(),
                limit.div_ceil(2)
            );
        }
    }
    #[test]
    fn continuous_deadlines_include_sleep_and_do_not_spin_on_unread_expired_timerfd() {
        let mut k = spinning_peers();
        let p = k.processes.get_mut(&1).unwrap();
        let fd = p.linux.vfs.timerfd(1, 0).unwrap();
        p.linux
            .vfs
            .timerfd_set(fd, Duration::ZERO, Some(Instant::now()))
            .unwrap();
        p.poll_deadline = Some(Instant::now());
        assert_eq!(k.parallel_timeout(), Duration::from_millis(10));
        k.processes.get_mut(&1).unwrap().state = State::Blocked(63, [0; 6]);
        assert_eq!(k.parallel_timeout(), Duration::from_millis(10));
        k.processes.get_mut(&1).unwrap().state = State::Blocked(22, [0; 6]);
        assert_eq!(k.parallel_timeout(), Duration::ZERO);
        k.processes.get_mut(&1).unwrap().state =
            State::Sleep(Instant::now() + Duration::from_millis(1), 0);
        assert!(k.parallel_timeout() <= Duration::from_millis(1));
    }
    #[test]
    fn socket_timeout_wakes_continuous_peers_but_not_stopped_or_runnable_tasks() {
        let mut k = spinning_peers();
        k.processes.get_mut(&1).unwrap().socket_deadline = Some(Instant::now());
        assert_eq!(k.parallel_timeout(), Duration::from_millis(10));
        k.processes.get_mut(&1).unwrap().state = State::Blocked(212, [0; 6]);
        assert_eq!(k.parallel_timeout(), Duration::ZERO);
        k.processes.get_mut(&1).unwrap().state = State::Stopped;
        assert_eq!(k.parallel_timeout(), Duration::from_millis(10));
    }
    #[test]
    fn prctl_seccomp_modes_translate_to_syscall_operations() {
        let mut k = kernel();
        let mut p = process();
        let mut io = trames_linux::CapturedOutput::default();
        assert_eq!(
            k.syscall(1, &mut p, 167, [22, 0, 0, 0, 0, 0], &mut io),
            Err(Errno(22))
        );
        assert_eq!(
            k.syscall(1, &mut p, 167, [22, 1, 0xffff, 0, 0, 0], &mut io),
            Ok(Some(0))
        );
        assert_eq!(p.seccomp.mode(), 1);
        let mut p = process();
        p.memory.write(0x1000, &[1, 0, 0, 0, 0, 0, 0, 0]).unwrap();
        write_u64(&mut p.memory, 0x1008, 0x1100).unwrap();
        p.memory
            .write(0x1100, &[6, 0, 0, 0, 0, 0, 0xff, 0x7f])
            .unwrap();
        assert_eq!(
            k.syscall(1, &mut p, 167, [22, 2, 0x1000, 0, 0, 0], &mut io),
            Ok(Some(0))
        );
        assert_eq!(p.seccomp.mode(), 2);
        assert_eq!(p.seccomp.count(), 1);
    }
    #[test]
    fn gpu_wait_defers_caught_handlers_but_preserves_stop_continue() {
        let mut p = process();
        let mut k = kernel();
        p.state = State::Gpu {
            job: 1,
            pc: 0x4000,
            operation: 27,
            instructions: 0,
            began: Instant::now(),
        };
        p.actions[10].handler = 0x5000;
        Kernel::queue_one(1, &mut p, 10, 1);
        assert!(!k.deliver(1, &mut p).unwrap());
        assert!(p.pending.contains_key(&10));
        Kernel::queue_one(1, &mut p, 19, 1);
        assert!(k.deliver(1, &mut p).unwrap());
        assert!(matches!(p.state, State::Stopped));
        assert!(matches!(
            p.stopped_state.as_deref(),
            Some(State::Gpu { job: 1, .. })
        ));
        Kernel::queue_one(1, &mut p, 18, 1);
        assert!(matches!(p.state, State::Gpu { job: 1, .. }));
    }
    #[test]
    fn a_removed_gpu_context_completes_once_without_reexecuting_the_stub() {
        let mut p = process();
        let mut k = kernel();
        k.instructions = 123;
        p.state = State::Gpu {
            job: 1,
            pc: 0x4000,
            operation: 27,
            instructions: 10,
            began: Instant::now(),
        };
        assert!(k.resume_gpu(1, &mut p));
        assert!(matches!(p.state, State::Ready));
        assert_eq!(p.cpu.pc, 0x4004);
        assert_eq!(p.cpu.x(0), (-4i64) as u64);
        assert_eq!(k.gpu_peer_instructions, 113);
        assert_eq!(k.instructions, 124);
        assert!(!k.resume_gpu(1, &mut p));
        assert_eq!(k.instructions, 124);
    }
    #[test]
    fn session_leader_fifo_open_waits_before_terminal_inspection_and_is_interruptible() {
        let mut p = process();
        let mut k = kernel();
        let mut io = trames_linux::CapturedOutput::default();
        p.linux.vfs.mknod_at(-100, b"/fifo", 0o10600, 0).unwrap();
        p.memory.write(0x1000, b"/fifo\0").unwrap();
        let args = [(-100i64) as u64, 0x1000, 0, 0, 0, 0];
        k.call(1, &mut p, 56, args, &mut io);
        assert!(matches!(p.state, State::Blocked(56, _)));
        let hidden = p.pending_open.unwrap();
        assert!(p.linux.vfs.fd_kind(hidden).is_err());
        p.actions[10].handler = 0x4000;
        Kernel::queue_one(1, &mut p, 10, 1);
        assert!(k.deliver(1, &mut p).unwrap());
        assert!(p.pending_open.is_none());
        assert!(p.linux.vfs.fifo_open_ready(hidden).is_err());
        // The handler's saved ucontext contains EINTR, not a reserved fd/EBADF.
        let mut result = [0; 8];
        p.memory.read(p.cpu.sp + 312, &mut result).unwrap();
        assert_eq!(u64::from_le_bytes(result), error(Errno(4)));
    }
    #[test]
    fn failed_blocked_epoll_clears_deadline_and_restores_its_temporary_signal_mask() {
        let mut p = process();
        let mut k = kernel();
        p.poll_deadline = Some(Instant::now());
        p.poll_mask = Some(0x200);
        p.mask = 0x400;
        let a = [999, 0x4800, 1, u64::MAX, 0, 8];
        p.state = State::Blocked(22, a);
        k.call(
            1,
            &mut p,
            22,
            a,
            &mut trames_linux::CapturedOutput::default(),
        );
        assert!(matches!(p.state, State::Ready));
        assert_eq!(p.cpu.x(0), error(Errno(9)));
        assert!(p.poll_deadline.is_none());
        assert!(p.poll_mask.is_none());
        assert_eq!(p.mask, 0x200);
    }
    #[test]
    fn stdio_eagain_blocks_only_without_nonblocking_flag() {
        struct Pending;
        impl GuestOutput for Pending {
            fn write(&mut self, _: OutputFd, _: &[u8]) -> std::io::Result<usize> {
                Err(std::io::ErrorKind::WouldBlock.into())
            }
            fn poll_stdin(&mut self, _: &mut [u8]) -> std::io::Result<usize> {
                Err(std::io::ErrorKind::WouldBlock.into())
            }
        }
        let mut k = kernel();
        let mut p = process();
        p.memory.write(0x1000, b"a").unwrap();
        let mut io = Pending;
        for (number, fd, flags) in [(64, 1, 1), (63, 0, 0)] {
            let args = [fd, 0x1000, 1, 0, 0, 0];
            k.call(1, &mut p, number, args, &mut io);
            assert!(matches!(p.state, State::Blocked(n, _) if n == number));
            p.linux
                .vfs
                .set_status_flags(fd as u32, flags | O_NONBLOCK)
                .unwrap();
            k.call(1, &mut p, number, args, &mut io);
            assert!(matches!(p.state, State::Ready));
            assert_eq!(p.cpu.x(0), error(Errno(11)));
        }
    }

    #[test]
    fn splice_blocks_on_backpressure_and_respects_output_nonblocking() {
        let mut k = kernel();
        let mut p = process();
        let mut io = trames_linux::CapturedOutput::default();
        let [r, w] = p.linux.vfs.pipe(0).unwrap();
        let [r2, w2] = p.linux.vfs.pipe(0).unwrap();
        p.linux.vfs.write(w, b"abc").unwrap();
        while p.linux.vfs.write(w2, &[0; 4096]).is_ok() {}
        let a = [r as u64, 0, w2 as u64, 0, 3, 0];
        k.call(1, &mut p, 76, a, &mut io);
        assert!(matches!(p.state, State::Blocked(76, _)));
        p.linux.vfs.set_status_flags(w2, 1 | O_NONBLOCK).unwrap();
        k.call(1, &mut p, 76, a, &mut io);
        assert!(matches!(p.state, State::Ready));
        assert_eq!(p.cpu.x(0), error(Errno(11)));
        p.linux.vfs.read(r2, 3).unwrap();
        k.call(1, &mut p, 76, a, &mut io);
        assert!(matches!(p.state, State::Ready));
        assert_eq!(p.cpu.x(0), 3);
        assert_eq!(p.linux.vfs.read(r, 3), Err(Errno(11)));
    }
    #[test]
    fn close_range_unshare_detaches_only_callers_shared_table() {
        let mut k = kernel();
        let mut p = process();
        let mut io = trames_linux::CapturedOutput::default();
        let fd = p.linux.vfs.open_at(-100, b"/file", 0x42, 0o600).unwrap();
        k.processes.insert(2, p.fork());
        assert_eq!(
            k.syscall(
                1,
                &mut p,
                436,
                [fd as u64, u32::MAX as u64, 8, 0, 0, 0],
                &mut io
            ),
            Err(Errno(22))
        );
        assert_eq!(p.files_id, 1);
        assert_eq!(
            k.syscall(
                1,
                &mut p,
                436,
                [fd as u64, u32::MAX as u64, 2, 0, 0, 0],
                &mut io
            ),
            Ok(Some(0))
        );
        k.sync_resources(&p);
        assert_ne!(p.files_id, k.processes[&2].files_id);
        assert_eq!(p.linux.vfs.fd_kind(fd), Err(Errno(9)));
        assert!(k.processes[&2].linux.vfs.fd_kind(fd).is_ok());
        let fd = p.linux.vfs.open_at(-100, b"/file", 0, 0).unwrap();
        assert_eq!(
            k.syscall(1, &mut p, 436, [fd as u64, fd as u64, 4, 0, 0, 0], &mut io),
            Ok(Some(0))
        );
        assert!(p.linux.vfs.fd_kind(fd).is_ok());
        p.linux.vfs.close_exec();
        assert_eq!(p.linux.vfs.fd_kind(fd), Err(Errno(9)));
    }
    #[test]
    fn unsupported_instruction_log_survives_child_exit() {
        let mut k = kernel();
        let parent = process();
        let mut child = parent.fork();
        k.synchronous_fault(
            2,
            &mut child,
            &trames_cpu::CpuFault::Instruction {
                pc: 0x4560,
                instruction: 0x2518e3e0,
            },
        );
        let text = String::from_utf8(parent.linux.vfs.syslog(3, 4096).unwrap()).unwrap();
        assert!(text.contains("pid=2"));
        assert!(text.contains("0x2518e3e0"));
        assert!(text.contains("0x4560"));
    }
    #[test]
    fn thread_directed_stop_and_continue_change_the_entire_group() {
        let mut k = kernel();
        let leader = process();
        let mut thread = leader.fork();
        thread.shared_pending = leader.shared_pending.clone();
        k.processes.insert(1, leader);
        Kernel::queue_one(2, &mut thread, 19, 1);
        assert!(k.deliver(2, &mut thread).unwrap());
        assert!(matches!(thread.state, State::Stopped));
        assert!(matches!(k.processes[&1].state, State::Stopped));
        k.processes.insert(2, thread);
        let mut caller = process();
        caller.tgid = 3;
        assert_eq!(k.thread_signal(3, &mut caller, 2, Some(1), 18), Ok(0));
        assert!(matches!(k.processes[&1].state, State::Ready));
        assert!(matches!(k.processes[&2].state, State::Ready));
        assert_eq!(k.processes[&1].wait_event, Some(0xffff));
    }
    #[test]
    fn process_signals_are_single_delivery_and_thread_signals_check_identity_and_permissions() {
        let mut p = process();
        let mut k = kernel();
        let mut thread = p.fork();
        thread.tgid = 1;
        k.processes.insert(2, thread);
        assert_eq!(k.signal(1, &mut p, 0, 10), Ok(0));
        assert!(p.shared_pending.borrow().contains_key(&10));
        assert!(k.processes[&2].pending.is_empty());
        assert_eq!(k.thread_signal(1, &mut p, 2, Some(3), 10), Err(Errno(3)));
        assert_eq!(k.thread_signal(1, &mut p, 2, Some(1), 10), Ok(0));
        assert_eq!(k.processes[&2].pending[&10].code, -6);
        p.linux.vfs.credentials = trames_vfs::Credentials::user(1000, 0);
        assert_eq!(k.thread_signal(1, &mut p, 2, None, 0), Err(Errno(1)));
        assert_eq!(k.thread_signal(1, &mut p, 2, None, 18), Ok(0));
        assert_eq!(k.thread_signal(1, &mut p, 0, None, 10), Err(Errno(22)));
    }
    #[test]
    fn signal_frame_restores_integer_vector_fp_state_and_accepts_guest_context_edits() {
        let mut p = process();
        let mut k = kernel();
        p.cpu.set_x(5, 55);
        p.cpu.fpsr = 17;
        p.cpu.fpcr = 1 << 22;
        p.cpu.set_vector(8, 0x0123456789abcdef0011223344556677);
        p.mask = 1 << 11;
        p.actions[10] = Action {
            handler: 0x5000,
            flags: 4,
            ..Action::default()
        };
        Kernel::queue_one(1, &mut p, 10, 2);
        assert!(k.deliver(1, &mut p).unwrap());
        let sp = p.cpu.sp;
        assert_eq!(p.cpu.x(0), 10);
        assert_eq!(p.cpu.x(1), sp);
        assert_eq!(p.cpu.x(2), sp + 128);
        assert_eq!(p.cpu.x(30), 0x7fff_ffd0_0000);
        assert_ne!(p.mask & (1 << 9), 0);
        write_u64(&mut p.memory, sp + 312 + 5 * 8, 77).unwrap();
        p.cpu.set_vector(8, 0);
        p.cpu.fpcr = 0;
        p.cpu.fpsr = 0;
        Kernel::sigreturn(&mut p).unwrap();
        assert_eq!(p.cpu.x(5), 77);
        assert_eq!(p.cpu.sp, 0x9000);
        assert_eq!(p.cpu.pc, 0x4000);
        assert_eq!(p.cpu.vector(8), 0x0123456789abcdef0011223344556677);
        assert_eq!(p.cpu.fpcr, 1 << 22);
        assert_eq!(p.cpu.fpsr, 17);
        assert_eq!(p.mask, 1 << 11);
    }
    #[test]
    fn malformed_signal_frame_fails_without_partial_register_restoration() {
        let mut p = process();
        let mut k = kernel();
        p.actions[10] = Action {
            handler: 0x5000,
            ..Action::default()
        };
        Kernel::queue_one(1, &mut p, 10, 2);
        k.deliver(1, &mut p).unwrap();
        let sp = p.cpu.sp;
        p.memory.write(sp + 592, &[0; 4]).unwrap();
        let old = p.cpu.clone();
        assert_eq!(Kernel::sigreturn(&mut p), Err(Errno(14)));
        assert_eq!(p.cpu.pc, old.pc);
        assert_eq!(p.cpu.sp, old.sp);
        assert_eq!(p.cpu.x(0), old.x(0));
    }
    #[test]
    fn stop_continue_preserves_a_blocked_timer() {
        let mut p = process();
        let mut k = kernel();
        let deadline = Instant::now() + Duration::from_secs(1);
        p.state = State::Sleep(deadline, 0);
        Kernel::queue_one(1, &mut p, 19, 1);
        k.deliver(1, &mut p).unwrap();
        assert!(matches!(p.state, State::Stopped));
        Kernel::queue_one(1, &mut p, 18, 1);
        assert!(matches!(p.state,State::Sleep(d,_) if d==deadline));
    }
    #[test]
    fn restart_rewinds_the_syscall_and_eintr_does_not() {
        for restart in [false, true] {
            let mut p = process();
            let mut k = kernel();
            p.cpu.pc = 0x4004;
            p.state = State::Blocked(63, [3, 0x1000, 7, 0, 0, 0]);
            p.actions[10] = Action {
                handler: 0x5000,
                flags: if restart { 0x10000000 } else { 0 },
                ..Action::default()
            };
            Kernel::queue_one(1, &mut p, 10, 2);
            k.deliver(1, &mut p).unwrap();
            Kernel::sigreturn(&mut p).unwrap();
            assert_eq!(p.cpu.pc, if restart { 0x4000 } else { 0x4004 });
            assert_eq!(p.cpu.x(0), if restart { 3 } else { error(Errno(4)) });
        }
    }
}

#[cfg(test)]
mod boundary_tests {
    use super::tests::{kernel, process};
    use super::*;
    use trames_linux::CapturedOutput;
    #[test]
    fn kill_is_delivered_even_with_an_older_pending_signal_while_stopped() {
        let mut p = process();
        let mut k = kernel();
        p.state = State::Stopped;
        Kernel::queue_one(1, &mut p, 10, 2);
        Kernel::queue_one(1, &mut p, 9, 2);
        k.deliver(1, &mut p).unwrap();
        assert!(matches!(p.state, State::Zombie(9)));
    }
    #[test]
    fn process_limit_rejects_fork_without_changing_parent_memory() {
        let mut p = process();
        let mut k = kernel();
        for id in 2..=MAX_PROCESSES as u32 {
            k.processes.insert(id, process());
        }
        let before = p.memory.mapped_bytes();
        assert_eq!(
            k.syscall(
                1,
                &mut p,
                220,
                [17, 0, 0, 0, 0, 0],
                &mut CapturedOutput::default()
            ),
            Err(Errno(11))
        );
        assert_eq!(p.memory.mapped_bytes(), before);
    }
    #[test]
    fn failed_exec_preserves_image_and_cloexec_descriptor() {
        let mut p = process();
        p.linux
            .vfs
            .import_executable(b"/bad", b"not ELF".to_vec())
            .unwrap();
        let fd = p.linux.vfs.open_at(-100, b"/bad", 0x80000, 0).unwrap();
        p.memory.write(0x1000, b"/bad\0").unwrap();
        p.memory.write(0x1100, &[0; 8]).unwrap();
        assert_eq!(
            Kernel::exec(&mut p, [0x1000, 0x1100, 0, 0, 0, 0], TOTAL_MAPPED_LIMIT),
            Err(Errno(8))
        );
        assert_eq!(p.cpu.pc, 0x4000);
        assert!(p.linux.vfs.fd_kind(fd).is_ok());
    }
    #[test]
    fn terminal_resize_event_is_scoped_to_the_foreground_group() {
        struct Events(Option<TerminalEvent>);
        impl GuestOutput for Events {
            fn write(&mut self, _: OutputFd, b: &[u8]) -> std::io::Result<usize> {
                Ok(b.len())
            }
            fn poll_stdin(&mut self, _: &mut [u8]) -> std::io::Result<usize> {
                Err(std::io::ErrorKind::WouldBlock.into())
            }
            fn terminal_event(&mut self) -> Option<TerminalEvent> {
                self.0.take()
            }
        }
        let mut k = kernel();
        k.terminal = Some(Terminal::default());
        k.processes.insert(1, process());
        let mut other = process();
        other.group = 2;
        k.processes.insert(2, other);
        assert!(k
            .pump_terminal(&mut Events(Some(TerminalEvent::Resize {
                rows: 40,
                columns: 120
            })))
            .unwrap());
        assert_eq!(&k.terminal.as_ref().unwrap().window[..4], &[40, 0, 120, 0]);
        assert!(k.processes[&1].pending.contains_key(&28));
        assert!(!k.processes[&2].pending.contains_key(&28));
    }
}

#[cfg(test)]
mod cpu_fault_tests {
    use super::tests::{kernel, process};
    use super::*;
    use trames_cpu::CpuFault;
    use trames_memory::{MemoryError, Permissions};
    #[test]
    fn synchronous_signal_contains_fault_address_and_resumes_from_edited_context() {
        let mut k = kernel();
        let mut p = process();
        p.actions[11] = Action {
            handler: 0x2000,
            flags: 4,
            restorer: 0,
            mask: 0,
        };
        p.cpu.pc = 0x1330;
        p.cpu.set_x(7, 0x123456);
        let fault = CpuFault::Memory {
            pc: 0x1330,
            error: MemoryError::Permission {
                address: 0x3456,
                access: Access::Write,
            },
        };
        Kernel::queue_one(1, &mut p, 11, 2);
        k.synchronous_fault(1, &mut p, &fault);
        assert!(matches!(p.state, State::Ready));
        assert_eq!(p.cpu.x(0), 11);
        assert_eq!(p.cpu.pc, 0x2000);
        let frame = p.cpu.sp;
        assert_eq!(
            p.memory.read_array::<4>(frame + 8).unwrap(),
            2i32.to_le_bytes()
        );
        assert_eq!(p.memory.read_u64(frame + 16).unwrap(), 0x3456);
        assert_eq!(p.memory.read_u64(frame + 304).unwrap(), 0x3456);
        assert_eq!(p.memory.read_u64(frame + 568).unwrap(), 0x1330);
        p.memory
            .write_array(frame + 568, 0x1334u64.to_le_bytes())
            .unwrap();
        Kernel::sigreturn(&mut p).unwrap();
        assert_eq!(p.cpu.pc, 0x1334);
        assert_eq!(p.cpu.x(7), 0x123456);
        assert_eq!(p.frames, 0);
    }
    #[test]
    fn masked_or_ignored_synchronous_faults_terminate_instead_of_looping() {
        for (blocked, handler) in [(false, 1), (true, 0x2000)] {
            let mut k = kernel();
            let mut p = process();
            p.actions[5].handler = handler;
            if blocked {
                p.mask |= 1 << 4;
            }
            let fault = CpuFault::Trap {
                pc: 0x1000,
                instruction: 0xd4200020,
                signal: 5,
                code: 1,
            };
            k.synchronous_fault(2, &mut p, &fault);
            assert!(matches!(p.state, State::Zombie(5)));
        }
    }
    #[test]
    fn data_top_byte_tags_work_across_pages_and_fork_copy_on_write() {
        let mut m = Memory::default();
        m.map(0x1000, 8192, Permissions::RW).unwrap();
        let tagged = 0xab00000000001ffc;
        m.write_array(tagged, 12345u64.to_le_bytes()).unwrap();
        assert_eq!(m.read_u64(0x1ffc).unwrap(), 12345);
        let mut child = m.clone();
        child.write_array(tagged, 6789u64.to_le_bytes()).unwrap();
        assert_eq!(m.read_u64(tagged).unwrap(), 12345);
        assert_eq!(child.read_u64(tagged).unwrap(), 6789);
        assert!(
            m.fetch_u32(tagged).is_err(),
            "data tags do not authorize tagged code addresses"
        );
    }
}
