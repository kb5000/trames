// SPDX-License-Identifier: GPL-3.0-or-later
//! Cooperative virtual processes. Guest fork/exec never create a host process.
use super::{Exit, Runtime, RuntimeError};
use crate::terminal::Terminal;
use std::{
    collections::BTreeMap,
    time::{Duration, Instant},
};
use trames_cpu::{Cpu, SliceOutcome};
use trames_linux::{GuestOutput, Linux, OutputFd, SyscallOutcome, TerminalEvent, MAX_WRITE};
use trames_memory::{Access, Memory};
use trames_vfs::{Errno, FdKind, O_NONBLOCK};

const MAX_PROCESSES: usize = 32;
const TOTAL_MAPPED_LIMIT: usize = 256 * 1024 * 1024;
const UNBLOCKABLE: u64 = (1 << 8) | (1 << 18);
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
    status: i32,
    code: i32,
}
#[derive(Clone)]
enum State {
    Ready,
    Blocked(u32, [u64; 6]),
    Sleep(Instant, u64),
    Suspend(u64),
    Stopped,
    Zombie(i32),
}
struct Process {
    cpu: Cpu,
    memory: Memory,
    linux: Linux,
    parent: u32,
    group: u32,
    session: u32,
    state: State,
    actions: [Action; 65],
    mask: u64,
    pending: BTreeMap<u32, Info>,
    frames: u32,
    wait_event: Option<i32>,
    poll_deadline: Option<Instant>,
    poll_mask: Option<u64>,
    stopped_state: Option<Box<State>>,
    auto_reap: bool,
}
impl Process {
    fn fork(&self) -> Self {
        Self {
            cpu: self.cpu.clone(),
            memory: self.memory.clone(),
            linux: self.linux.clone(),
            parent: 0,
            group: self.group,
            session: self.session,
            state: State::Ready,
            actions: self.actions,
            mask: self.mask,
            pending: BTreeMap::new(),
            frames: self.frames,
            wait_event: None,
            poll_deadline: None,
            poll_mask: None,
            stopped_state: None,
            auto_reap: false,
        }
    }
}
struct Kernel {
    processes: BTreeMap<u32, Process>,
    next_pid: u32,
    terminal: Option<Terminal>,
    started: Instant,
    instructions: u64,
}
fn error(e: Errno) -> u64 {
    0u64.wrapping_sub(e.0 as u64)
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
    fn notify(&mut self, parent: u32, child: u32, status: i32, code: i32) {
        if let Some(p) = self.processes.get_mut(&parent) {
            if p.actions[17].handler == 1 || (matches!(code, 5 | 6) && p.actions[17].flags & 1 != 0)
            {
                return;
            }
            p.pending.entry(17).or_insert(Info {
                sender: child,
                status,
                code,
            });
        }
    }
    fn exit(&mut self, pid: u32, p: &mut Process, status: i32) {
        p.auto_reap = self.processes.get(&p.parent).is_some_and(|parent| {
            parent.actions[17].handler == 1 || parent.actions[17].flags & 2 != 0
        });
        p.linux.exit_tid(&mut p.memory);
        if pid != 1 {
            p.memory = Memory::default();
        }
        p.state = State::Zombie(status);
        p.linux.vfs.close_all();
        p.pending.clear();
        p.wait_event = None;
        self.notify(
            p.parent,
            pid,
            if status & 127 == 0 {
                status >> 8
            } else {
                status & 127
            },
            if status & 127 == 0 { 1 } else { 2 },
        );
        for child in self.processes.values_mut() {
            if child.parent == pid {
                child.parent = 1;
            }
        }
    }
    fn queue_one(pid: u32, p: &mut Process, sig: u32, sender: u32) {
        if sig == 0 || matches!(p.state, State::Zombie(_)) {
            return;
        }
        if sig == 18 {
            p.pending.retain(|s, _| !matches!(s, 19..=22));
            if matches!(p.state, State::Stopped) {
                p.state = p.stopped_state.take().map_or(State::Ready, |s| *s);
                p.wait_event = Some(0xffff);
            }
        }
        if matches!(sig, 19..=22) {
            p.pending.remove(&18);
        }
        if sig != 9 && sig != 19 && p.actions[sig as usize].handler == 1 {
            return;
        }
        let _ = pid;
        p.pending.entry(sig).or_insert(Info {
            sender,
            status: 0,
            code: 0,
        });
    }
    fn signal(&mut self, pid: u32, p: &mut Process, target: i32, sig: u32) -> Result<u64, Errno> {
        if sig > 64 {
            return Err(Errno(22));
        }
        let mut found = false;
        let current_group = p.group;
        let matches = |id: u32, group: u32| {
            if target > 0 {
                id == target as u32
            } else if target == 0 {
                group == current_group
            } else if target == -1 {
                true
            } else {
                group == target.unsigned_abs()
            }
        };
        if matches(pid, p.group) {
            found = true;
            Self::queue_one(pid, p, sig, pid);
        }
        for (&id, other) in &mut self.processes {
            if matches(id, other.group) {
                found = true;
                Self::queue_one(id, other, sig, pid);
            }
        }
        if found {
            Ok(0)
        } else {
            Err(Errno(3))
        }
    }
    fn deliver(&mut self, pid: u32, p: &mut Process) -> Result<bool, Errno> {
        let stopped = matches!(p.state, State::Stopped);
        let sig = if p.pending.contains_key(&9) {
            Some(9)
        } else {
            p.pending
                .keys()
                .copied()
                .find(|s| (!stopped || *s == 18) && (p.mask & (1u64 << (s - 1)) == 0 || *s == 19))
        };
        let Some(sig) = sig else { return Ok(false) };
        if matches!(p.state, State::Stopped) && sig != 9 && sig != 18 {
            return Ok(false);
        }
        let info = p.pending.remove(&sig).expect("pending");
        let action = p.actions[sig as usize];
        if sig == 9 || (action.handler == 0 && !matches!(sig, 17 | 18 | 23 | 28 | 19..=22)) {
            self.exit(pid, p, sig as i32);
            return Ok(true);
        }
        if sig == 19 || (action.handler == 0 && matches!(sig, 20..=22)) {
            if !matches!(p.state, State::Stopped) {
                p.stopped_state = Some(Box::new(p.state.clone()));
            }
            p.state = State::Stopped;
            p.wait_event = Some(((sig as i32) << 8) | 0x7f);
            self.notify(p.parent, pid, sig as i32, 5);
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
                if let Some(mask) = p.poll_mask.take() {
                    p.mask = mask;
                }
                p.poll_deadline = None;
                if action.flags & 0x10000000 != 0
                    && matches!(
                        number,
                        32 | 63 | 64 | 66 | 260 | 202 | 242 | 206 | 207 | 211 | 212
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
            _ => {}
        }
        let sp = p.cpu.sp.checked_sub(4688).ok_or(Errno(14))? & !15;
        Linux::check(&p.memory, sp, 4688, Access::Write)?;
        let mut frame = vec![0; 4688];
        frame[0..4].copy_from_slice(&sig.to_le_bytes());
        frame[8..12].copy_from_slice(&info.code.to_le_bytes());
        frame[16..20].copy_from_slice(&info.sender.to_le_bytes());
        frame[20..24].copy_from_slice(&1000u32.to_le_bytes());
        frame[24..28].copy_from_slice(&info.status.to_le_bytes());
        frame[168..176].copy_from_slice(&p.mask.to_le_bytes());
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
            if action.restorer == 0 {
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
        for r in 0..31 {
            p.cpu.set_x(r, u64_at(312 + r * 8));
        }
        p.cpu.sp = new_sp;
        p.cpu.pc = new_pc;
        p.cpu.flags = trames_cpu::Flags {
            n: u64_at(576) & (1 << 31) != 0,
            z: u64_at(576) & (1 << 30) != 0,
            c: u64_at(576) & (1 << 29) != 0,
            v: u64_at(576) & (1 << 28) != 0,
        };
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
        p.frames -= 1;
        p.state = State::Ready;
        Ok(())
    }
    fn exec(p: &mut Process, a: [u64; 6], budget: usize) -> Result<(), Errno> {
        let mut path = Linux::path(&p.memory, a[0])?;
        let mut argv = strings(&p.memory, a[1])?;
        let envp = strings(&p.memory, a[2])?;
        if argv.is_empty() {
            argv.push(Vec::new());
        }
        for depth in 0..5 {
            let bytes = p.linux.vfs.executable(&path)?;
            if bytes.starts_with(b"#!") {
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
                next.push(path);
                next.extend(argv.into_iter().skip(1));
                argv = next;
                path = interpreter.to_vec();
                continue;
            }
            let mut runtime =
                Runtime::load_guest(&bytes, &argv, &envp, &p.linux.vfs).map_err(|e| match e {
                    RuntimeError::Arguments(_) => Errno(7),
                    RuntimeError::Memory(_) => Errno(12),
                    RuntimeError::Elf(trames_elf::ElfError::Interpreter(n)) => Errno(n),
                    _ => Errno(8),
                })?;
            if runtime.memory.mapped_bytes() > budget {
                return Err(Errno(12));
            }
            p.linux.vfs.close_exec();
            std::mem::swap(&mut runtime.linux.vfs, &mut p.linux.vfs);
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
            p.state = State::Ready;
            return Ok(());
        }
        Err(Errno(40))
    }
    fn wait(&mut self, pid: u32, p: &mut Process, a: [u64; 6]) -> Result<Option<u64>, Errno> {
        if a[2] & !11 != 0 {
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
                c.parent == pid
                    && (target == -1
                        || (target == 0 && c.group == p.group)
                        || (target < -1 && c.group == target.unsigned_abs())
                        || (target > 0 && **id == target as u32))
            })
            .map(|(&id, _)| id)
            .collect();
        if matching.is_empty() {
            return Err(Errno(10));
        }
        for child in matching {
            let c = &self.processes[&child];
            let status = match c.state {
                State::Zombie(s) => Some(s),
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
                return Ok(Some(child as u64));
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
            93 | 94 => {
                self.exit(pid, p, (a[0] as u8 as i32) << 8);
                Ok(None)
            }
            172 | 178 => Ok(Some(pid as u64)),
            173 => Ok(Some(p.parent as u64)),
            96 => {
                p.linux.dispatch(&mut p.cpu, &mut p.memory, io);
                Ok(Some(pid as u64))
            }
            220 => {
                const ALLOWED: u64 = 0xff | 0x80000 | 0x100000 | 0x1000000 | 0x200000;
                if a[0] & !ALLOWED != 0 || a[0] & 255 != 17 {
                    return Err(Errno(22));
                }
                if self.processes.len() + 1 >= MAX_PROCESSES
                    || self
                        .processes
                        .values()
                        .map(|p| p.memory.mapped_bytes())
                        .sum::<usize>()
                        + p.memory.mapped_bytes() * 2
                        > TOTAL_MAPPED_LIMIT
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
                self.next_pid = self
                    .next_pid
                    .checked_add(1)
                    .filter(|v| *v < i32::MAX as u32)
                    .ok_or(Errno(11))?;
                let mut child = p.fork();
                child
                    .linux
                    .fork_tid(if a[0] & 0x200000 != 0 { a[4] } else { 0 });
                child.parent = pid;
                child.cpu.set_x(0, 0);
                if a[1] != 0 {
                    child.cpu.sp = a[1];
                }
                if a[0] & 0x80000 != 0 {
                    child.cpu.tpidr_el0 = a[3];
                }
                if a[0] & 0x100000 != 0 {
                    p.memory
                        .write(a[2], &id.to_le_bytes())
                        .map_err(|_| Errno(14))?;
                }
                if a[0] & 0x1000000 != 0 {
                    child
                        .memory
                        .write(a[4], &id.to_le_bytes())
                        .map_err(|_| Errno(14))?;
                }
                self.processes.insert(id, child);
                Ok(Some(id as u64))
            }
            221 => {
                let others = self
                    .processes
                    .values()
                    .map(|p| p.memory.mapped_bytes())
                    .sum::<usize>();
                Self::exec(p, a, TOTAL_MAPPED_LIMIT.saturating_sub(others))?;
                Ok(None)
            }
            260 => self.wait(pid, p, a),
            72 | 73 => self.poll(p, number, a, io),
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
                    let flags = read_u64(&p.memory, a[1] + 8)?;
                    if flags & 0x08000000 != 0 {
                        return Err(Errno(95));
                    } // Alternate signal stacks are not enabled in M2.
                    if flags & !0xd4000007 != 0 {
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
                    if action.handler == 1 {
                        p.pending.remove(&sig);
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
                let bits = p.pending.keys().fold(0, |v, s| v | (1u64 << (s - 1)));
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
            130 => self.signal(pid, p, a[0] as i32, a[1] as u32).map(Some),
            131 => {
                if a[0] as u32 != a[1] as u32 {
                    return Err(Errno(3));
                }
                self.signal(pid, p, a[0] as i32, a[2] as u32).map(Some)
            }
            154 => {
                let target = if a[0] == 0 { pid } else { a[0] as u32 };
                let group = if a[1] == 0 { target } else { a[1] as u32 };
                if a[1] > i32::MAX as u64 {
                    return Err(Errno(22));
                }
                let (session, parent) = if target == pid {
                    (p.session, p.parent)
                } else {
                    let c = self.processes.get(&target).ok_or(Errno(3))?;
                    (c.session, c.parent)
                };
                if target != pid && parent != pid {
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
                if target == pid {
                    p.group = group;
                } else {
                    self.processes.get_mut(&target).expect("target").group = group;
                }
                Ok(Some(0))
            }
            155 | 156 => {
                let id = if a[0] == 0 { pid } else { a[0] as u32 };
                let (group, session) = if id == pid {
                    (p.group, p.session)
                } else {
                    let c = self.processes.get(&id).ok_or(Errno(3))?;
                    (c.group, c.session)
                };
                Ok(Some(if number == 155 { group } else { session } as u64))
            }
            157 => {
                if p.group == pid || self.processes.values().any(|c| c.group == pid) {
                    return Err(Errno(1));
                }
                p.session = pid;
                p.group = pid;
                Ok(Some(pid as u64))
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
            113 => {
                if !matches!(a[0], 0 | 1 | 4 | 6 | 7) {
                    return Err(Errno(22));
                }
                Linux::check(&p.memory, a[1], 16, Access::Write)?;
                let elapsed = if matches!(a[0], 0 | 6) {
                    std::time::SystemTime::now()
                        .duration_since(std::time::UNIX_EPOCH)
                        .map_err(|_| Errno(5))?
                } else {
                    self.started.elapsed()
                };
                write_u64(&mut p.memory, a[1], elapsed.as_secs())?;
                write_u64(&mut p.memory, a[1] + 8, elapsed.subsec_nanos() as u64)?;
                Ok(Some(0))
            }
            124 => Ok(Some(0)),
            203 if matches!(p.state, State::Blocked(203, _)) => {
                Ok(p.linux.resume_connect(a[0] as u32)?.then_some(0))
            }
            63 | 64 | 66 | 29
                if self.terminal.is_some()
                    && p.linux.vfs.fd_kind(a[0] as u32)? == FdKind::Terminal =>
            {
                self.terminal_call(pid, p, number, a, io)
            }
            _ => {
                let trace = p.linux.trace;
                p.linux.trace = false;
                let outcome = p.linux.dispatch(&mut p.cpu, &mut p.memory, io);
                p.linux.trace = trace;
                if let SyscallOutcome::Exit(code) = outcome {
                    self.exit(pid, p, (code as i32) << 8);
                    return Ok(None);
                }
                let raw = p.cpu.x(0);
                let per_call_nonblock = match number {
                    206 | 207 => a[3] & 0x40 != 0,
                    211 | 212 => a[2] & 0x40 != 0,
                    _ => false,
                };
                if ((raw == error(Errno(11))
                    && matches!(number, 63 | 64 | 66 | 202 | 242 | 206 | 207 | 211 | 212)
                    && !per_call_nonblock)
                    || (raw == error(Errno(115)) && number == 203)
                    || (raw == error(Errno(11)) && number == 32 && a[1] & 4 == 0))
                    && (number == 32 || p.linux.vfs.flags(a[0] as u32)? & O_NONBLOCK == 0)
                {
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
        let other_bytes = self
            .processes
            .values()
            .filter(|p| !matches!(p.state, State::Zombie(_)))
            .map(|p| p.memory.mapped_bytes())
            .sum::<usize>();
        let _ = p.memory.set_limit(
            trames_memory::DEFAULT_MEMORY_LIMIT.min(TOTAL_MAPPED_LIMIT.saturating_sub(other_bytes)),
        );
        let was_blocked = matches!(p.state, State::Blocked(..));
        p.cpu.set_x(8, number as u64);
        put_args(&mut p.cpu, a);
        let result = self.syscall(pid, p, number, a, io);
        let raw = match result {
            Ok(Some(raw)) => {
                p.cpu.set_x(0, raw);
                p.state = State::Ready;
                Some(raw)
            }
            Ok(None) => {
                if matches!(p.state, State::Ready) && !matches!(number, 221 | 139 | 93 | 94) {
                    p.state = State::Blocked(number, a);
                    put_args(&mut p.cpu, a);
                }
                None
            }
            Err(e) => {
                let raw = error(e);
                p.cpu.set_x(0, raw);
                p.state = State::Ready;
                Some(raw)
            }
        };
        let signal_pipe = match number {
            64 | 66 => true,
            206 => a[3] & 0x4000 == 0,
            211 => a[2] & 0x4000 == 0,
            _ => false,
        };
        if signal_pipe && raw == Some(error(Errno(32))) {
            Self::queue_one(pid, p, 13, pid);
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
                let t = self.terminal.as_ref().ok_or(Errno(25))?;
                (events & 4) | if t.readable() { events & 1 } else { 0 }
            }
            _ => p.linux.vfs.poll(fd, events),
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
    fn terminal_call(
        &mut self,
        pid: u32,
        p: &mut Process,
        number: u32,
        a: [u64; 6],
        io: &mut impl GuestOutput,
    ) -> Result<Option<u64>, Errno> {
        let tty = self.terminal.as_mut().expect("terminal");
        if number == 63 {
            let count = a[2].min(MAX_WRITE as u64) as usize;
            Linux::check(&p.memory, a[1], count, Access::Write)?;
            if p.session == tty.session && p.group != tty.foreground {
                if p.mask & (1 << 20) != 0 || p.actions[21].handler == 1 {
                    return Err(Errno(5));
                }
                let group = p.group;
                let _ = self.signal(pid, p, -(group as i32), 21)?;
                return Ok(None);
            }
            match tty.read(count) {
                Ok(bytes) => {
                    p.memory.write(a[1], &bytes).map_err(|_| Errno(14))?;
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
            if p.session == tty.session
                && tty.lflag() & 256 != 0
                && p.group != tty.foreground
                && p.mask & (1 << 21) == 0
                && p.actions[22].handler != 1
            {
                let group = p.group;
                self.signal(pid, p, -(group as i32), 22)?;
                return Ok(None);
            }
            let count = tty.queue_output(&bytes)?;
            let _ = io;
            Ok(Some(count as u64))
        } else {
            let request = a[1] as u32;
            if matches!(request, 0x540f | 0x5410) && p.session != tty.session {
                return Err(Errno(25));
            }
            if p.session == tty.session
                && p.group != tty.foreground
                && matches!(request, 0x5402..=0x5404 | 0x5410)
                && p.mask & (1 << 21) == 0
                && p.actions[22].handler != 1
            {
                let group = p.group;
                self.signal(pid, p, -(group as i32), 22)?;
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
                        .write(a[2], &tty.foreground.to_le_bytes())
                        .map_err(|_| Errno(14))?;
                }
                0x5410 => {
                    let group = read_u32(&p.memory, a[2])?;
                    if group == 0 || group > i32::MAX as u32 {
                        return Err(Errno(22));
                    }
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
                        self.signal(pid, p, -(group as i32), 28)?;
                    }
                }
                0x541b => {
                    p.memory
                        .write(a[2], &(tty.available() as u32).to_le_bytes())
                        .map_err(|_| Errno(14))?;
                }
                0x540e => {
                    if pid != p.session {
                        return Err(Errno(1));
                    }
                    tty.session = p.session;
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
                    tty.output_queue.drain(..n);
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
    fn drive(&mut self, io: &mut impl GuestOutput, limit: u64) -> Result<Exit, RuntimeError> {
        loop {
            let mut progress = self.pump_terminal(io)?;
            let ids: Vec<u32> = self.processes.keys().copied().collect();
            for id in ids {
                let Some(mut p) = self.processes.remove(&id) else {
                    continue;
                };
                if !matches!(p.state, State::Zombie(_)) {
                    match self.deliver(id, &mut p) {
                        Ok(changed) => progress |= changed,
                        Err(_) => {
                            self.exit(id, &mut p, 11);
                            progress = true;
                        }
                    }
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
                    if matches!(p.state, State::Ready) {
                        if self.instructions >= limit {
                            self.processes.insert(id, p);
                            return Err(RuntimeError::BudgetExceeded(limit));
                        }
                        match p
                            .cpu
                            .run_slice(&mut p.memory, (limit - self.instructions).min(4096))
                        {
                            SliceOutcome::Fault(e) => {
                                if p.linux.trace {
                                    eprintln!("trames pid {id}: {e}");
                                }
                                if id == 1 {
                                    self.processes.insert(id, p);
                                    return Err(RuntimeError::Cpu(e));
                                }
                                let sig = if matches!(e, trames_cpu::CpuFault::Memory { .. }) {
                                    11
                                } else {
                                    4
                                };
                                self.exit(id, &mut p, sig);
                            }
                            SliceOutcome::Yield { instructions } => {
                                self.instructions += instructions
                            }
                            SliceOutcome::Syscall { instructions } => {
                                self.instructions += instructions;
                                let number = p.cpu.x(8) as u32;
                                let a = args(&p.cpu);
                                self.call(id, &mut p, number, a, io);
                            }
                        }
                        progress = true;
                    }
                }
                let status = if let State::Zombie(s) = p.state {
                    Some(s)
                } else {
                    None
                };
                if !p.auto_reap || id == 1 {
                    self.processes.insert(id, p);
                }
                if id == 1 {
                    if let Some(status) = status {
                        // Session teardown is explicit: root exit cancels remaining virtual children.
                        while self
                            .terminal
                            .as_ref()
                            .is_some_and(|t| !t.output_queue.is_empty())
                        {
                            if !self.pump_terminal(io)? {
                                break;
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
                        || matches!(p.state, State::Sleep(..))
                        || matches!(p.state, State::Blocked(63, a)
                        if p.linux.vfs.fd_kind(a[0] as u32).ok() == Some(FdKind::Stdin))
                        || matches!(p.state, State::Blocked(number, a)
                        if matches!(number, 63 | 64 | 66 | 202 | 203 | 242 | 206 | 207 | 211 | 212)
                        && p.linux.vfs.fd_kind(a[0] as u32).ok() == Some(FdKind::Socket))
                        || (matches!(p.state, State::Blocked(72 | 73, _))
                            && p.linux.vfs.live_sockets() != 0)
                }) || self.terminal.is_some();
                if !external {
                    return Err(RuntimeError::Deadlock);
                }
                self.wait_external();
            }
        }
    }
    fn wait_external(&self) {
        let mut sockets = Vec::new();
        let now = Instant::now();
        let mut timeout = Duration::from_millis(20);
        if self.terminal.is_some() {
            timeout = Duration::from_millis(1);
        }
        for p in self.processes.values() {
            if let Some(deadline) = p.poll_deadline {
                timeout = timeout.min(deadline.saturating_duration_since(now));
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
                63 | 202 | 242 | 207 | 212 => {
                    add(a[0] as u32, 1);
                    if p.linux.vfs.fd_kind(a[0] as u32).ok() == Some(FdKind::Stdin) {
                        timeout = timeout.min(Duration::from_millis(1));
                    }
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
    }
}
pub(super) fn run(
    runtime: &mut Runtime,
    io: &mut impl GuestOutput,
    limit: u64,
) -> Result<Exit, RuntimeError> {
    let root = Process {
        cpu: std::mem::take(&mut runtime.cpu),
        memory: std::mem::take(&mut runtime.memory),
        linux: std::mem::replace(&mut runtime.linux, Linux::new(0)),
        parent: 0,
        group: 1,
        session: 1,
        state: State::Ready,
        actions: [Action::default(); 65],
        mask: 0,
        pending: BTreeMap::new(),
        frames: 0,
        wait_event: None,
        poll_deadline: None,
        poll_mask: None,
        stopped_state: None,
        auto_reap: false,
    };
    let mut kernel = Kernel {
        processes: BTreeMap::from([(1, root)]),
        next_pid: 2,
        terminal: runtime.terminal.take(),
        started: Instant::now(),
        instructions: 0,
    };
    let result = kernel.drive(io, limit);
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
    pub(super) fn process() -> Process {
        let mut memory = Memory::default();
        memory.map(0x1000, 8 * PAGE_SIZE, Permissions::RW).unwrap();
        Process {
            cpu: Cpu::new(0x4000, 0x9000),
            memory,
            linux: Linux::new(0xa000),
            parent: 0,
            group: 1,
            session: 1,
            state: State::Ready,
            actions: [Action::default(); 65],
            mask: 0,
            pending: BTreeMap::new(),
            frames: 0,
            wait_event: None,
            poll_deadline: None,
            poll_mask: None,
            stopped_state: None,
            auto_reap: false,
        }
    }
    pub(super) fn kernel() -> Kernel {
        Kernel {
            processes: BTreeMap::new(),
            next_pid: 2,
            terminal: None,
            started: Instant::now(),
            instructions: 0,
        }
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
        for id in 2..=32 {
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
