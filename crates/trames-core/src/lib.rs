// SPDX-License-Identifier: GPL-3.0-or-later
//! ELF, initial Linux stack, CPU slices, and syscall dispatch composition.
use std::fmt;
use trames_cpu::{Cpu, CpuFault, SliceOutcome};
use trames_elf::{ElfError, LoadedElf};
use trames_linux::{GuestOutput, SyscallOutcome};
use trames_memory::{Memory, MemoryError, Permissions, DEFAULT_MEMORY_LIMIT, PAGE_SIZE};

pub const STACK_TOP: u64 = 0x0000_7fff_fff0_0000;
pub const STACK_SIZE: u64 = 64 * 1024;
pub const DEFAULT_INSTRUCTION_LIMIT: u64 = 10_000_000;

#[derive(Debug)]
pub enum RuntimeError {
    Elf(ElfError),
    Memory(MemoryError),
    Arguments(&'static str),
    Cpu(CpuFault),
    BudgetExceeded(u64),
}
impl fmt::Display for RuntimeError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Elf(e) => e.fmt(f),
            Self::Memory(e) => e.fmt(f),
            Self::Cpu(e) => e.fmt(f),
            Self::Arguments(s) => write!(f, "initial stack: {s}"),
            Self::BudgetExceeded(n) => write!(f, "guest instruction limit exceeded ({n})"),
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
}
#[derive(Debug, PartialEq, Eq)]
pub struct Exit {
    pub code: u8,
    pub instructions: u64,
}

fn push_string(memory: &mut Memory, cursor: &mut u64, string: &str) -> Result<u64, RuntimeError> {
    if string.as_bytes().contains(&0) {
        return Err(RuntimeError::Arguments("NUL in argv or envp"));
    }
    let len = string
        .len()
        .checked_add(1)
        .ok_or(RuntimeError::Arguments("string size"))?;
    *cursor = cursor
        .checked_sub(len as u64)
        .filter(|&address| address >= STACK_TOP - STACK_SIZE)
        .ok_or(RuntimeError::Arguments("stack exceeds 64 KiB"))?;
    memory.write(*cursor, string.as_bytes())?;
    memory.write(*cursor + string.len() as u64, &[0])?;
    Ok(*cursor)
}

fn initial_stack(
    elf: &mut LoadedElf,
    argv: &[String],
    envp: &[String],
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
    // Linux-compatible scalar auxv for static M0 binaries; no fabricated entropy.
    let mut auxv = vec![
        (6u64, PAGE_SIZE),
        (9, elf.entry),
        (11, 1000),
        (12, 1000),
        (13, 1000),
        (14, 1000),
        (23, 0),
        (31, arguments[0]),
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
        .filter(|&v| v >= STACK_TOP - STACK_SIZE)
        .ok_or(RuntimeError::Arguments("stack exceeds 64 KiB"))?;
    for (index, word) in words.iter().enumerate() {
        elf.memory
            .write(sp + index as u64 * 8, &word.to_le_bytes())?;
    }
    Ok(sp)
}

impl Runtime {
    pub fn load(bytes: &[u8], argv: &[String], envp: &[String]) -> Result<Self, RuntimeError> {
        let mut elf = trames_elf::load(bytes, DEFAULT_MEMORY_LIMIT)?;
        let sp = initial_stack(&mut elf, argv, envp)?;
        let cpu = Cpu::new(elf.entry, sp);
        Ok(Self {
            cpu,
            memory: elf.memory,
        })
    }

    pub fn run(
        &mut self,
        output: &mut impl GuestOutput,
        instruction_limit: u64,
    ) -> Result<Exit, RuntimeError> {
        let mut instructions = 0;
        while instructions < instruction_limit {
            match self.cpu.run_slice(
                &mut self.memory,
                (instruction_limit - instructions).min(4096),
            ) {
                SliceOutcome::Fault(e) => return Err(RuntimeError::Cpu(e)),
                SliceOutcome::Yield {
                    instructions: count,
                } => instructions += count,
                SliceOutcome::Syscall {
                    instructions: count,
                } => {
                    instructions += count;
                    if let SyscallOutcome::Exit(code) =
                        trames_linux::dispatch(&mut self.cpu, &self.memory, output)
                    {
                        return Ok(Exit { code, instructions });
                    }
                }
            }
        }
        Err(RuntimeError::BudgetExceeded(instruction_limit))
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
        let mut runtime = Runtime { cpu, memory };
        let mut out = trames_linux::CapturedOutput::default();
        assert!(matches!(
            runtime.run(&mut out, 17),
            Err(RuntimeError::BudgetExceeded(17))
        ));
    }
}
