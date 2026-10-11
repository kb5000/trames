// SPDX-License-Identifier: GPL-3.0-or-later
//! ELF, initial Linux stack, CPU slices, and syscall dispatch composition.
use std::fmt;
use trames_cpu::{Cpu, CpuFault};
use trames_elf::{ElfError, LoadedElf};
use trames_linux::{GuestOutput, Linux};
mod process;
mod terminal;
use trames_memory::{Memory, MemoryError, Permissions, DEFAULT_MEMORY_LIMIT, PAGE_SIZE};

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
    )
}
fn initial_stack_bytes(
    elf: &mut LoadedElf,
    argv: &[Vec<u8>],
    envp: &[Vec<u8>],
    credentials: &trames_vfs::Credentials,
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
    for value in argv {
        arguments.push(push_string(&mut elf.memory, &mut cursor, value)?);
    }
    for value in envp {
        environment.push(push_string(&mut elf.memory, &mut cursor, value)?);
    }
    cursor -= 16;
    let mut entropy = [0; 16];
    trames_linux::fill_entropy(&mut entropy)
        .map_err(|_| RuntimeError::Arguments("host entropy source unavailable"))?;
    elf.memory.write(cursor, &entropy)?;
    // Static Linux auxv, including real host entropy for the guest stack guard.
    let mut auxv = vec![
        (6u64, PAGE_SIZE),
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
        let sp = initial_stack_bytes(&mut elf, argv, envp, &vfs.credentials)?;
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
        Ok(Self {
            cpu: Cpu::new(elf.start_entry, sp),
            memory: elf.memory,
            linux,
            terminal: None,
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
        let argv: Vec<_> = argv.iter().map(|s| s.as_bytes().to_vec()).collect();
        let envp: Vec<_> = envp.iter().map(|s| s.as_bytes().to_vec()).collect();
        let mut runtime = Self::load_guest(bytes, &argv, &envp, &vfs)?;
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
        assert_eq!(e.memory.read_u64(sp + 24).unwrap(), 0);
        assert_eq!(e.memory.read_u64(sp + 40).unwrap(), 0);
        assert_eq!(e.memory.read_u64(sp + 48).unwrap(), 6);
        assert_eq!(e.memory.read_u64(sp + 56).unwrap(), PAGE_SIZE);
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
        };
        let mut out = trames_linux::CapturedOutput::default();
        assert!(matches!(
            runtime.run(&mut out, 17),
            Err(RuntimeError::BudgetExceeded(17))
        ));
    }
}
