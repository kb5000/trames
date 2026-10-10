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
    fn read_stdin(&mut self, bytes: &mut [u8]) -> io::Result<usize> {
        io::stdin().lock().read(bytes)
    }
    fn write(&mut self, fd: OutputFd, bytes: &[u8]) -> io::Result<usize> {
        match fd {
            OutputFd::Stdout => io::stdout().lock().write(bytes),
            OutputFd::Stderr => io::stderr().lock().write(bytes),
        }
    }
}

fn run() -> Result<u8, Box<dyn std::error::Error>> {
    let mut args = env::args_os()
        .skip(1)
        .map(|arg| arg.into_string().map_err(|_| "arguments must be UTF-8"))
        .collect::<Result<Vec<_>, _>>()?
        .into_iter()
        .peekable();
    let mut budget = DEFAULT_INSTRUCTION_LIMIT;
    let mut trace = false;
    let mut files = Vec::new();
    if args.peek().is_some_and(|a| a == "--help" || a == "-h") {
        println!("Usage: trames-run [--max-instructions N] [--trace-syscalls] [--file /GUEST=HOST] [--] ELF [ARG ...]\nStatic ELF64/AArch64 reference interpreter with a bounded virtual filesystem.");
        return Ok(0);
    }
    while let Some(option) = args.peek() {
        match option.as_str() {
            "--max-instructions" => {
                args.next();
                budget = args.next().ok_or("missing instruction limit")?.parse()?;
                if budget == 0 {
                    return Err("instruction limit must be positive".into());
                }
            }
            "--trace-syscalls" => {
                args.next();
                trace = true;
            }
            "--file" => {
                args.next();
                let mapping = args.next().ok_or("missing /GUEST=HOST mapping")?;
                let (guest, host) = mapping
                    .split_once('=')
                    .ok_or("file mapping requires /GUEST=HOST")?;
                files.push((guest.to_string(), host.to_string()));
            }
            _ => break,
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
    runtime.linux.trace = trace;
    for (guest, host) in files {
        let mut content = Vec::new();
        File::open(host)?
            .take(trames_vfs::MAX_FILE_BYTES as u64 + 1)
            .read_to_end(&mut content)?;
        if content.len() > trames_vfs::MAX_FILE_BYTES {
            return Err("imported file exceeds 16 MiB limit".into());
        }
        runtime.linux.vfs.import_file(guest.as_bytes(), content)?;
    }
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
