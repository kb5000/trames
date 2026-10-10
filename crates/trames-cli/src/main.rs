// SPDX-License-Identifier: GPL-3.0-or-later
use std::{
    env,
    fs::File,
    io::{self, Read, Write},
    process::ExitCode,
};
use trames_core::{Runtime, DEFAULT_INSTRUCTION_LIMIT};
use trames_elf::MAX_ELF_SIZE;
use trames_linux::{GuestOutput, OutputFd};

struct HostOutput;
impl GuestOutput for HostOutput {
    fn write(&mut self, fd: OutputFd, bytes: &[u8]) -> io::Result<usize> {
        match fd {
            OutputFd::Stdout => io::stdout().lock().write(bytes),
            OutputFd::Stderr => io::stderr().lock().write(bytes),
        }
    }
}

fn run() -> Result<u8, Box<dyn std::error::Error>> {
    let mut args = env::args().skip(1).peekable();
    let mut budget = DEFAULT_INSTRUCTION_LIMIT;
    if args.peek().is_some_and(|a| a == "--help" || a == "-h") {
        println!("Usage: trames-run [--max-instructions N] [--] ELF [ARG ...]\nM0: static ELF64/AArch64, bounded reference interpreter, write/exit syscalls.");
        return Ok(0);
    }
    if args.peek().is_some_and(|a| a == "--max-instructions") {
        args.next();
        budget = args.next().ok_or("missing instruction limit")?.parse()?;
        if budget == 0 {
            return Err("instruction limit must be positive".into());
        }
    }
    if args.peek().is_some_and(|a| a == "--") {
        args.next();
    }
    let path = args
        .next()
        .ok_or("Usage: trames-run [--max-instructions N] [--] ELF [ARG ...]")?;
    let mut argv = vec![path.clone()];
    argv.extend(args);
    let file = File::open(&path)?;
    let mut bytes = Vec::new();
    file.take(MAX_ELF_SIZE as u64 + 1).read_to_end(&mut bytes)?;
    if bytes.len() > MAX_ELF_SIZE {
        return Err("ELF exceeds 64 MiB file limit".into());
    }
    // A deterministic guest environment; host secrets are never inherited.
    let envp = vec!["PATH=/usr/bin:/bin".into(), "LANG=C".into()];
    let mut runtime = Runtime::load(&bytes, &argv, &envp)?;
    let exit = runtime.run(&mut HostOutput, budget)?;
    io::stdout().flush()?;
    io::stderr().flush()?;
    Ok(exit.code)
}

fn main() -> ExitCode {
    match run() {
        Ok(code) => ExitCode::from(code),
        Err(e) => {
            eprintln!("trames-run: {e}");
            ExitCode::from(125)
        }
    }
}
