// SPDX-License-Identifier: GPL-3.0-or-later
use std::{
    collections::VecDeque,
    env,
    fs::File,
    io::{self, IsTerminal, Read, Write},
    process::{Command, ExitCode, Stdio},
    sync::mpsc::{self, Receiver, TryRecvError},
};
use trames_core::{Runtime, DEFAULT_INSTRUCTION_LIMIT};
use trames_elf::MAX_ELF_SIZE;
use trames_linux::{GuestOutput, OutputFd};

struct HostOutput {
    input: Receiver<io::Result<Vec<u8>>>,
    pending: VecDeque<u8>,
    eof: bool,
}
impl HostOutput {
    fn new() -> Self {
        let (tx, input) = mpsc::sync_channel(8);
        std::thread::spawn(move || {
            let mut stdin = io::stdin().lock();
            loop {
                let mut bytes = vec![0; 4096];
                let item = match stdin.read(&mut bytes) {
                    Ok(n) => {
                        bytes.truncate(n);
                        Ok(bytes)
                    }
                    Err(e) => Err(e),
                };
                let done = item.as_ref().map_or(true, Vec::is_empty);
                if tx.send(item).is_err() || done {
                    break;
                }
            }
        });
        Self {
            input,
            pending: VecDeque::new(),
            eof: false,
        }
    }
}
impl GuestOutput for HostOutput {
    fn poll_stdin(&mut self, bytes: &mut [u8]) -> io::Result<usize> {
        if bytes.is_empty() {
            return Ok(0);
        }
        if self.pending.is_empty() && !self.eof {
            match self.input.try_recv() {
                Ok(Ok(input)) => {
                    if input.is_empty() {
                        self.eof = true;
                    } else {
                        self.pending.extend(input);
                    }
                }
                Ok(Err(e)) => return Err(e),
                Err(TryRecvError::Empty) => return Err(io::ErrorKind::WouldBlock.into()),
                Err(TryRecvError::Disconnected) => self.eof = true,
            }
        }
        let n = bytes.len().min(self.pending.len());
        for b in &mut bytes[..n] {
            *b = self.pending.pop_front().expect("pending");
        }
        Ok(n)
    }
    fn stdin_ready(&mut self) -> io::Result<bool> {
        if !self.pending.is_empty() || self.eof {
            return Ok(true);
        }
        match self.input.try_recv() {
            Ok(Ok(bytes)) => {
                if bytes.is_empty() {
                    self.eof = true;
                } else {
                    self.pending.extend(bytes);
                }
                Ok(true)
            }
            Ok(Err(e)) => Err(e),
            Err(TryRecvError::Empty) => Ok(false),
            Err(TryRecvError::Disconnected) => {
                self.eof = true;
                Ok(true)
            }
        }
    }
    fn write(&mut self, fd: OutputFd, bytes: &[u8]) -> io::Result<usize> {
        match fd {
            OutputFd::Stdout => {
                let mut stream = io::stdout().lock();
                let n = stream.write(bytes)?;
                stream.flush()?;
                Ok(n)
            }
            OutputFd::Stderr => {
                let mut stream = io::stderr().lock();
                let n = stream.write(bytes)?;
                stream.flush()?;
                Ok(n)
            }
        }
    }
}

struct RawTerminal(Option<String>);
impl RawTerminal {
    fn enter(enabled: bool) -> io::Result<Self> {
        if !enabled || !io::stdin().is_terminal() {
            return Ok(Self(None));
        }
        let saved = Command::new("stty")
            .arg("-g")
            .stdin(Stdio::inherit())
            .output()?;
        if !saved.status.success() {
            return Err(io::Error::other("cannot read host terminal settings"));
        }
        let state = String::from_utf8(saved.stdout)
            .map_err(io::Error::other)?
            .trim()
            .to_owned();
        if !Command::new("stty")
            .args(["raw", "-echo"])
            .status()?
            .success()
        {
            return Err(io::Error::other("cannot set host terminal raw mode"));
        }
        Ok(Self(Some(state)))
    }
}
impl Drop for RawTerminal {
    fn drop(&mut self) {
        if let Some(state) = &self.0 {
            let _ = Command::new("stty").arg(state).status();
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
    let mut pty = false;
    let mut install_busybox = false;
    let mut files = Vec::new();
    if args.peek().is_some_and(|a| a == "--help" || a == "-h") {
        println!("Usage: trames-run [--max-instructions N] [--trace-syscalls] [--file /GUEST=HOST] [--exec-file /GUEST=HOST] [--install-busybox] [--pty] [--] ELF [ARG ...]\nStatic ELF64/AArch64 interpreter with virtual processes, files and a PTY.");
        return Ok(0);
    }
    while let Some(option) = args.peek() {
        match option.as_str() {
            "--pty" => {
                args.next();
                pty = true;
            }
            "--install-busybox" => {
                args.next();
                install_busybox = true;
            }
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
            "--file" | "--exec-file" => {
                let executable = option == "--exec-file";
                args.next();
                let mapping = args.next().ok_or("missing /GUEST=HOST mapping")?;
                let (guest, host) = mapping
                    .split_once('=')
                    .ok_or("file mapping requires /GUEST=HOST")?;
                files.push((guest.to_string(), host.to_string(), executable));
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
    if install_busybox {
        runtime.linux.vfs.import_file(b"/bin/busybox", bytes)?;
        runtime.linux.vfs.mkdir_at(-100, b"/usr", 0o755)?;
        runtime.linux.vfs.mkdir_at(-100, b"/usr/bin", 0o755)?;
        for applet in [
            "sh", "ash", "busybox", "echo", "printf", "true", "false", "cat", "wc", "head", "tail",
            "pwd", "ls", "basename", "dirname", "sleep", "kill", "seq", "env", "test", "mkdir",
            "rm", "touch",
        ] {
            for directory in ["/bin", "/usr/bin"] {
                let alias = format!("{directory}/{applet}");
                if alias != "/bin/busybox" {
                    runtime
                        .linux
                        .vfs
                        .import_alias(b"/bin/busybox", alias.as_bytes(), true)?;
                }
            }
        }
    }
    for (guest, host, executable) in files {
        let mut content = Vec::new();
        File::open(host)?
            .take(trames_vfs::MAX_FILE_BYTES as u64 + 1)
            .read_to_end(&mut content)?;
        if content.len() > trames_vfs::MAX_FILE_BYTES {
            return Err("imported file exceeds 16 MiB limit".into());
        }
        if executable {
            runtime
                .linux
                .vfs
                .import_executable(guest.as_bytes(), content)?;
        } else {
            runtime.linux.vfs.import_file(guest.as_bytes(), content)?;
        }
    }
    if pty {
        runtime.enable_terminal(trames_core::Terminal::default())?;
    }
    let _host_terminal = RawTerminal::enter(pty)?;
    let exit = runtime.run(&mut HostOutput::new(), budget)?;
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
