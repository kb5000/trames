// SPDX-License-Identifier: GPL-3.0-or-later
//! ELF, initial Linux stack, CPU slices, and syscall dispatch composition.
use std::fmt;
use trames_cpu::{Cpu, CpuFault};
use trames_elf::{ElfError, LoadedElf};
use trames_linux::{GuestOutput, Linux};
mod process;
mod terminal;
mod workers;
use trames_memory::{Memory, MemoryError, Permissions, DEFAULT_MEMORY_LIMIT, PAGE_SIZE};

#[derive(Default, Clone, Copy)]
pub struct RuntimeOptions {
    /// Explicit host opt-in to the bounded NativeCall v1 pure profile.
    pub pure_accelerator: bool,
    /// Experimental bsearch wrapper routing; default retains original musl.
    pub native_bsearch: bool,
}

pub const STACK_TOP: u64 = 0x0000_7fff_fff0_0000;
// Alpine BusyBox's interactive line editor probes a >64 KiB stack frame.
// This mapping still counts against the existing per-process memory quota.
pub const STACK_SIZE: u64 = 1024 * 1024;
const INITIAL_ARGUMENT_BYTES: u64 = 64 * 1024;
pub const DEFAULT_INSTRUCTION_LIMIT: u64 = 10_000_000;

#[derive(Debug)]
pub enum RuntimeError {
    Elf(ElfError),
    Memory(MemoryError),
    Arguments(&'static str),
    Cpu(CpuFault),
    BudgetExceeded(u64),
    Deadlock,
    Cancelled,
}
impl fmt::Display for RuntimeError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Elf(e) => e.fmt(f),
            Self::Memory(e) => e.fmt(f),
            Self::Cpu(e) => e.fmt(f),
            Self::Arguments(s) => write!(f, "initial stack: {s}"),
            Self::BudgetExceeded(n) => write!(f, "guest instruction limit exceeded ({n})"),
            Self::Deadlock => write!(
                f,
                "all virtual processes are blocked without an event source"
            ),
            Self::Cancelled => write!(f, "session cancelled by its host"),
        }
    }
}
impl std::error::Error for RuntimeError {}
impl From<ElfError> for RuntimeError {
    fn from(e: ElfError) -> Self {
        Self::Elf(e)
    }
}
impl From<MemoryError> for RuntimeError {
    fn from(e: MemoryError) -> Self {
        Self::Memory(e)
    }
}

pub struct Runtime {
    pub cpu: Cpu,
    pub memory: Memory,
    pub linux: Linux,
    pub terminal: Option<terminal::Terminal>,
    pub metrics: RuntimeMetrics,
    pub profile_syscalls: bool,
    /// Virtual CPU worker limit; 1 retains the existing local fast path.
    pub cpu_count: usize,
}
/// Host monotonic timings; these are diagnostics, never guest clock values.
#[derive(Default, Debug)]
pub struct RuntimeMetrics {
    pub cpu_count: usize,
    pub parallel_rounds: u64,
    pub worker_slices: u64,
    /// Worker-local CPU slices, including polls and native returns; no messages
    /// or barriers are required between these slices.
    pub worker_chunks: u64,
    pub peak_parallel_cpus: usize,
    pub peak_parallel_native_calls: usize,
    pub parallel_memory_time: std::time::Duration,
    pub parallel_dispatch_time: std::time::Duration,
    /// Time to drain peers after the first exit or a requested timed checkpoint.
    pub parallel_stop_time: std::time::Duration,
    pub cpu_time: std::time::Duration,
    pub syscall_time: std::time::Duration,
    pub idle_time: std::time::Duration,
    pub syscalls: u64,
    pub proc_refreshes: u64,
    pub context_switches: u64,
    pub decode_lookups: u64,
    pub instructions_decoded: u64,
    pub decode_misses: u64,
    pub native: trames_accel::native::Metrics,
}
pub use terminal::Terminal;
#[derive(Debug, PartialEq, Eq)]
pub struct Exit {
    pub code: u8,
    pub instructions: u64,
}

fn push_string(memory: &mut Memory, cursor: &mut u64, string: &[u8]) -> Result<u64, RuntimeError> {
    if string.contains(&0) {
        return Err(RuntimeError::Arguments("NUL in argv or envp"));
    }
    let len = string
        .len()
        .checked_add(1)
        .ok_or(RuntimeError::Arguments("string size"))?;
    *cursor = cursor
        .checked_sub(len as u64)
        .filter(|&address| address >= STACK_TOP - INITIAL_ARGUMENT_BYTES)
        .ok_or(RuntimeError::Arguments("stack exceeds 64 KiB"))?;
    memory.write(*cursor, string)?;
    memory.write(*cursor + string.len() as u64, &[0])?;
    Ok(*cursor)
}

#[cfg(test)]
fn initial_stack(
    elf: &mut LoadedElf,
    argv: &[String],
    envp: &[String],
) -> Result<u64, RuntimeError> {
    initial_stack_bytes(
        elf,
        &argv
            .iter()
            .map(|s| s.as_bytes().to_vec())
            .collect::<Vec<_>>(),
        &envp
            .iter()
            .map(|s| s.as_bytes().to_vec())
            .collect::<Vec<_>>(),
        &trames_vfs::Credentials::default(),
        RuntimeOptions::default(),
    )
}
fn initial_stack_bytes(
    elf: &mut LoadedElf,
    argv: &[Vec<u8>],
    envp: &[Vec<u8>],
    credentials: &trames_vfs::Credentials,
    options: RuntimeOptions,
) -> Result<u64, RuntimeError> {
    if argv.is_empty() {
        return Err(RuntimeError::Arguments("argv[0] is required"));
    }
    if argv.len() + envp.len() > 4096 {
        return Err(RuntimeError::Arguments(
            "too many arguments or environment entries",
        ));
    }
    elf.memory
        .map(STACK_TOP - STACK_SIZE, STACK_SIZE, Permissions::RW)?;
    let mut cursor = STACK_TOP;
    let mut arguments = Vec::new();
    let mut environment = Vec::new();
    // Linux places argv[0..] then envp[0..] in increasing address order.
    // libuv uses that contiguous region for process.title. Packing forwards
    // onto a descending stack reverses it and can underflow its area length.
    for value in envp.iter().rev() {
        environment.push(push_string(&mut elf.memory, &mut cursor, value)?);
    }
    environment.reverse();
    for value in argv.iter().rev() {
        arguments.push(push_string(&mut elf.memory, &mut cursor, value)?);
    }
    arguments.reverse();
    cursor -= 16;
    let mut entropy = [0; 16];
    trames_linux::fill_entropy(&mut entropy)
        .map_err(|_| RuntimeError::Arguments("host entropy source unavailable"))?;
    elf.memory.write(cursor, &entropy)?;
    // Static Linux auxv, including real host entropy for the guest stack guard.
    let mut auxv = vec![
        (6u64, PAGE_SIZE),
        (
            trames_accel::native::AUXV_KEY,
            if options.pure_accelerator {
                trames_accel::native::PROFILE
            } else {
                0
            },
        ),
        (
            trames_accel::native::FEATURES_AUXV_KEY,
            if options.pure_accelerator {
                trames_accel::native::FEATURES
            } else {
                0
            },
        ),
        (
            trames_accel::callbacks::AUXV_KEY,
            if options.pure_accelerator {
                trames_accel::callbacks::PROFILE
            } else {
                0
            },
        ),
        (
            trames_accel::callbacks::SEARCH_AUXV_KEY,
            u64::from(options.pure_accelerator && options.native_bsearch),
        ),
        (
            trames_accel::numeric::AUXV_KEY,
            if options.pure_accelerator {
                trames_accel::numeric::PROFILE
            } else {
                0
            },
        ),
        (
            trames_accel::integer::AUXV_KEY,
            if options.pure_accelerator {
                trames_accel::integer::PROFILE
            } else {
                0
            },
        ),
        (16, trames_cpu::HWCAP),
        (26, trames_cpu::HWCAP2),
        (9, elf.entry),
        (7, elf.interpreter_base),
        (11, credentials.uid as u64),
        (12, credentials.euid as u64),
        (13, credentials.gid as u64),
        (14, credentials.egid as u64),
        (
            23,
            u64::from(credentials.uid != credentials.euid || credentials.gid != credentials.egid),
        ),
        (31, arguments[0]),
        (25, cursor),
    ];
    if let Some(phdr) = elf.phdr {
        auxv.extend([(3, phdr), (4, 56), (5, elf.phnum as u64)]);
    }
    auxv.push((0, 0));
    let mut words = vec![argv.len() as u64];
    words.extend(arguments);
    words.push(0);
    words.extend(environment);
    words.push(0);
    for (key, value) in auxv {
        words.extend([key, value]);
    }
    let sp = cursor
        .checked_sub(words.len() as u64 * 8)
        .map(|v| v & !15)
        .filter(|&v| v >= STACK_TOP - INITIAL_ARGUMENT_BYTES)
        .ok_or(RuntimeError::Arguments("stack exceeds 64 KiB"))?;
    for (index, word) in words.iter().enumerate() {
        elf.memory
            .write(sp + index as u64 * 8, &word.to_le_bytes())?;
    }
    let trampoline = 0x0000_7fff_ffd0_0000;
    elf.memory.map(trampoline, PAGE_SIZE, Permissions::RW)?;
    elf.memory.write(
        trampoline,
        &[0x68, 0x11, 0x80, 0xd2, 0x01, 0x00, 0x00, 0xd4],
    )?;
    elf.memory.protect(trampoline, PAGE_SIZE, Permissions::RX)?;
    Ok(sp)
}

impl Runtime {
    pub fn enable_terminal(&mut self, terminal: Terminal) -> Result<(), trames_vfs::Errno> {
        self.linux.vfs.enable_terminal()?;
        self.terminal = Some(terminal);
        Ok(())
    }
    pub(crate) fn load_guest(
        bytes: &[u8],
        argv: &[Vec<u8>],
        envp: &[Vec<u8>],
        vfs: &trames_vfs::Vfs,
        options: RuntimeOptions,
    ) -> Result<Self, RuntimeError> {
        let mut elf = trames_elf::load_with_interpreter(bytes, DEFAULT_MEMORY_LIMIT, |path| {
            vfs.executable(path).map_err(|e| ElfError::Interpreter(e.0))
        })?;
        let mut linux = Linux::new(elf.brk_base);
        linux.arguments = argv.to_vec();
        linux.environment = envp.to_vec();
        linux.executable_path = argv.first().cloned().unwrap_or_default();
        linux.command_name = linux
            .executable_path
            .rsplit(|b| *b == b'/')
            .next()
            .unwrap_or_default()
            .iter()
            .copied()
            .take(15)
            .collect();
        if let Ok(path) = vfs.resolve(trames_vfs::AT_FDCWD, &linux.executable_path) {
            linux.executable_path = path;
        }
        let sp = initial_stack_bytes(&mut elf, argv, envp, &vfs.credentials, options)?;
        for i in 0..argv.len() {
            linux
                .argument_pointers
                .push(elf.memory.read_u64(sp + 8 + i as u64 * 8)?);
        }
        let mut aux = sp + 8 * (2 + argv.len() + envp.len() + 1) as u64;
        loop {
            let key = elf.memory.read_u64(aux)?;
            let value = elf.memory.read_u64(aux + 8)?;
            linux.auxiliary.extend(key.to_le_bytes());
            linux.auxiliary.extend(value.to_le_bytes());
            aux += 16;
            if key == 0 {
                break;
            }
        }
        let mut cpu = Cpu::new(elf.start_entry, sp);
        cpu.set_native_calls(options.pure_accelerator);
        Ok(Self {
            cpu,
            memory: elf.memory,
            linux,
            terminal: None,
            metrics: RuntimeMetrics::default(),
            profile_syscalls: false,
            cpu_count: std::thread::available_parallelism()
                .map_or(1, |n| n.get())
                .min(32),
        })
    }
    pub fn load(bytes: &[u8], argv: &[String], envp: &[String]) -> Result<Self, RuntimeError> {
        Self::load_with_vfs(bytes, argv, envp, trames_vfs::Vfs::default())
    }
    pub fn load_with_vfs(
        bytes: &[u8],
        argv: &[String],
        envp: &[String],
        vfs: trames_vfs::Vfs,
    ) -> Result<Self, RuntimeError> {
        Self::load_with_options(bytes, argv, envp, vfs, RuntimeOptions::default())
    }
    pub fn load_with_options(
        bytes: &[u8],
        argv: &[String],
        envp: &[String],
        vfs: trames_vfs::Vfs,
        options: RuntimeOptions,
    ) -> Result<Self, RuntimeError> {
        let argv: Vec<_> = argv.iter().map(|s| s.as_bytes().to_vec()).collect();
        let envp: Vec<_> = envp.iter().map(|s| s.as_bytes().to_vec()).collect();
        let mut runtime = Self::load_guest(bytes, &argv, &envp, &vfs, options)?;
        runtime.linux.vfs = vfs;
        Ok(runtime)
    }

    pub fn run(
        &mut self,
        output: &mut impl GuestOutput,
        instruction_limit: u64,
    ) -> Result<Exit, RuntimeError> {
        process::run(self, output, instruction_limit)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn virtual_devices_work_without_any_proc_access_or_snapshot_refresh() {
        // openat(AT_FDCWD,"/dev/zero",0); read(fd,path,8); exit(0)
        let words = [
            0x92800c60u32,
            0xd2800708,
            0xd4000001,
            0xd2800102,
            0xd28007e8,
            0xd4000001,
            0xd2800000,
            0xd2800ba8,
            0xd4000001,
        ];
        let mut memory = Memory::default();
        memory.map(0x1000, PAGE_SIZE, Permissions::RW).unwrap();
        memory.map(0x2000, PAGE_SIZE, Permissions::RW).unwrap();
        for (i, word) in words.iter().enumerate() {
            memory
                .write(0x1000 + i as u64 * 4, &word.to_le_bytes())
                .unwrap();
        }
        memory.protect(0x1000, PAGE_SIZE, Permissions::RX).unwrap();
        memory.write(0x2100, b"/dev/zero\0").unwrap();
        let mut cpu = Cpu::new(0x1000, 0);
        cpu.set_x(1, 0x2100);
        let mut runtime = Runtime {
            cpu,
            memory,
            linux: Linux::new(0),
            terminal: None,
            metrics: RuntimeMetrics::default(),
            profile_syscalls: false,
            cpu_count: 1,
        };
        let exit = runtime
            .run(&mut trames_linux::CapturedOutput::default(), 100)
            .unwrap();
        assert_eq!(exit.code, 0);
        assert_eq!(runtime.memory.read_array::<8>(0x2100).unwrap(), [0; 8]);
        assert_eq!(runtime.metrics.proc_refreshes, 1); // initial mount setup only
    }
    fn elf() -> LoadedElf {
        LoadedElf {
            memory: Memory::default(),
            entry: 0x4000,
            phdr: Some(0x4040),
            phnum: 2,
            start_entry: 0x4000,
            interpreter_base: 0,
            brk_base: 0x5000,
        }
    }
    #[test]
    fn stack_layout_contains_argv_envp_auxv_and_aligned_sp() {
        let mut e = elf();
        let sp = initial_stack(
            &mut e,
            &["program".into(), "value".into()],
            &["KEY=1".into()],
        )
        .unwrap();
        assert_eq!(sp % 16, 0);
        assert_eq!(e.memory.read_u64(sp).unwrap(), 2);
        let arg0 = e.memory.read_u64(sp + 8).unwrap();
        let mut name = [0; 8];
        e.memory.read(arg0, &mut name).unwrap();
        assert_eq!(&name, b"program\0");
        let arg1 = e.memory.read_u64(sp + 16).unwrap();
        let env0 = e.memory.read_u64(sp + 32).unwrap();
        assert_eq!(arg1, arg0 + 8);
        assert_eq!(env0, arg1 + 6);
        assert_eq!(env0 + 6, STACK_TOP);
        assert_eq!(e.memory.read_u64(sp + 24).unwrap(), 0);
        assert_eq!(e.memory.read_u64(sp + 40).unwrap(), 0);
        assert_eq!(e.memory.read_u64(sp + 48).unwrap(), 6);
        assert_eq!(e.memory.read_u64(sp + 56).unwrap(), PAGE_SIZE);
        let mut capabilities = [None, None];
        for i in 0..32 {
            let key = e.memory.read_u64(sp + 48 + i * 16).unwrap();
            let value = e.memory.read_u64(sp + 56 + i * 16).unwrap();
            match key {
                16 => capabilities[0] = Some(value),
                26 => capabilities[1] = Some(value),
                0 => break,
                _ => {}
            }
        }
        assert_eq!(
            capabilities,
            [Some(trames_cpu::HWCAP), Some(trames_cpu::HWCAP2)]
        );
        // PAC, SVE and MTE are not advertised; BF16 has functional execution.
        assert_eq!(trames_cpu::HWCAP & ((1 << 22) | (1 << 30) | (1 << 31)), 0);
        assert_eq!(trames_cpu::HWCAP2 & (1 << 18), 0);
        assert_ne!(trames_cpu::HWCAP2 & (1 << 14), 0);
        assert!(e.memory.fetch_u32(sp).is_err());
        assert!(e.memory.read_u64(STACK_TOP - STACK_SIZE - 8).is_err());
    }
    #[test]
    fn argument_limits_and_nuls_fail() {
        assert!(initial_stack(&mut elf(), &[], &[]).is_err());
        assert!(initial_stack(&mut elf(), &["a\0b".into()], &[]).is_err());
        assert!(initial_stack(&mut elf(), &["x".repeat(STACK_SIZE as usize)], &[]).is_err());
    }
    #[test]
    fn endless_guest_is_bounded() {
        let mut memory = Memory::default();
        memory.map(0x1000, PAGE_SIZE, Permissions::RW).unwrap();
        memory.write(0x1000, &0x14000000u32.to_le_bytes()).unwrap();
        memory.protect(0x1000, PAGE_SIZE, Permissions::RX).unwrap();
        let mut cpu = Cpu::default();
        cpu.pc = 0x1000;
        let mut runtime = Runtime {
            cpu,
            memory,
            linux: Linux::new(0x2000),
            terminal: None,
            metrics: RuntimeMetrics::default(),
            profile_syscalls: false,
            cpu_count: 1,
        };
        let mut out = trames_linux::CapturedOutput::default();
        assert!(matches!(
            runtime.run(&mut out, 17),
            Err(RuntimeError::BudgetExceeded(17))
        ));
    }
}

#[cfg(test)]
mod accelerator_tests {
    use super::*;
    fn runtime(len: u64, permissions: Permissions) -> Runtime {
        let mut memory = Memory::default();
        memory.map(0x1000, PAGE_SIZE, Permissions::RW).unwrap();
        memory.map(0x2000, PAGE_SIZE, permissions).unwrap();
        let words = [
            0xd2800030u32,
            0xf2a00030,
            0xd2800071,
            trames_accel::native::INSTRUCTION,
            0xd65f03c0,
            0xd503201f,
            0xd503201f,
            0xd503201f,
            0xd2800000,
            0xd2800ba8,
            0xd4000001,
        ];
        for (i, word) in words.into_iter().enumerate() {
            memory
                .write(0x1000 + i as u64 * 4, &word.to_le_bytes())
                .unwrap();
        }
        memory.protect(0x1000, PAGE_SIZE, Permissions::RX).unwrap();
        let mut cpu = Cpu::new(0x1000, 0x3000);
        cpu.set_native_calls(true);
        cpu.set_x(0, 0x2000);
        cpu.set_x(1, 0x91);
        cpu.set_x(2, len);
        cpu.set_x(19, 0xabcdef);
        cpu.set_x(30, 0x1020);
        cpu.tpidr_el0 = 0x9988;
        Runtime {
            cpu,
            memory,
            linux: Linux::new(0),
            terminal: None,
            metrics: RuntimeMetrics::default(),
            profile_syscalls: false,
            cpu_count: 1,
        }
    }
    #[test]
    fn committed_call_retires_once_and_preserves_guest_context() {
        let mut r = runtime(32, Permissions::RW);
        let exit = r
            .run(&mut trames_linux::CapturedOutput::default(), 20)
            .unwrap();
        assert_eq!(
            exit,
            Exit {
                code: 0,
                instructions: 8
            }
        );
        assert_eq!(r.memory.read_array::<32>(0x2000).unwrap(), [0x91; 32]);
        assert_eq!(r.cpu.x(19), 0xabcdef);
        assert_eq!(r.cpu.sp, 0x3000);
        assert_eq!(r.cpu.tpidr_el0, 0x9988);
        assert_eq!(r.metrics.native.calls, 1);
    }
    #[test]
    fn budget_checkpoint_fallback_and_fault_do_not_fake_retirement() {
        let mut r = runtime(32, Permissions::RW);
        assert!(matches!(
            r.run(&mut trames_linux::CapturedOutput::default(), 3),
            Err(RuntimeError::BudgetExceeded(3))
        ));
        assert_eq!(r.metrics.native.calls, 0);
        assert_eq!(r.cpu.pc, 0x100c);
        assert_eq!(r.memory.read_array::<32>(0x2000).unwrap(), [0; 32]);
        let mut r = runtime(
            trames_accel::native::MAX_BULK_BYTES as u64 + 1,
            Permissions::R,
        );
        assert!(matches!(
            r.run(&mut trames_linux::CapturedOutput::default(), 4),
            Err(RuntimeError::BudgetExceeded(4))
        ));
        assert_eq!(r.metrics.native.fallbacks, 1);
        assert_eq!(r.cpu.pc, 0x1010);
        assert_eq!(r.cpu.x(0), 0x2000);
        assert_eq!(r.cpu.x(1), 1);
        let mut r = runtime(32, Permissions::R);
        let exit = r
            .run(&mut trames_linux::CapturedOutput::default(), 4)
            .unwrap();
        assert_eq!(
            exit,
            Exit {
                code: 139,
                instructions: 3
            }
        );
        assert_eq!(r.cpu.pc, 0x100c);
        assert_eq!(r.cpu.x(1), 0x91);
        assert_eq!(r.metrics.native.faults, 1);
    }
}
