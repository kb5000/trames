// SPDX-License-Identifier: GPL-3.0-or-later
#![recursion_limit = "256"]
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
use trames_vfs::{DiskOptions, NetworkMode, StorageLimits, Vfs, VolumeSpace};

fn volume_space(path: &str) -> io::Result<VolumeSpace> {
    let path = std::fs::canonicalize(path)?;
    let output = Command::new("df")
        .arg("-Pk")
        .arg(path)
        .env("LC_ALL", "C")
        .output()?;
    if !output.status.success() {
        return Err(io::Error::other("cannot query storage volume"));
    }
    let text = String::from_utf8(output.stdout).map_err(io::Error::other)?;
    let fields: Vec<_> = text
        .lines()
        .last()
        .unwrap_or("")
        .split_whitespace()
        .collect();
    let index = fields
        .iter()
        .position(|s| s.ends_with('%'))
        .filter(|&i| i >= 3)
        .ok_or_else(|| io::Error::other("invalid df capacity response"))?;
    let number = |s: &str| {
        s.parse::<u64>()
            .ok()
            .and_then(|n| n.checked_mul(1024))
            .ok_or_else(|| io::Error::other("invalid volume size"))
    };
    Ok(VolumeSpace {
        total_bytes: number(fields[index - 3])?,
        available_bytes: number(fields[index - 1])?,
    })
}
fn storage_budget(value: &str) -> Result<u64, Box<dyn std::error::Error>> {
    let (number, scale) = [
        ("TiB", 1u64 << 40),
        ("GiB", 1 << 30),
        ("MiB", 1 << 20),
        ("KiB", 1 << 10),
    ]
    .into_iter()
    .find_map(|(suffix, scale)| value.strip_suffix(suffix).map(|n| (n, scale)))
    .unwrap_or((value, 1));
    let bytes = number
        .parse::<u64>()?
        .checked_mul(scale)
        .filter(|&n| n != 0 && n <= i64::MAX as u64)
        .ok_or("invalid storage budget")?;
    Ok(bytes)
}

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
    let backend = "cached";
    let mut metrics = false;
    let mut cpu_count = None;
    let mut pure_accelerator = false;
    let mut native_bsearch = false;
    let mut native_allocator = false;
    let mut vulkan_accelerator = false;
    let mut wayland = false;
    let mut pty = false;
    let mut install_busybox = false;
    let mut files = Vec::new();
    let mut exports = Vec::new();
    let mut storage = None;
    let mut memory_only = false;
    let mut memory_limits = trames_core::MemoryLimits::default();
    let mut disk_budget = None;
    let mut storage_info = false;
    let mut network = NetworkMode::Full;
    let mut virtual_network = None;
    let mut host_mappings = Vec::new();
    let mut rootfs = None;
    let mut musl_libc = None;
    let mut guest_path = false;
    let mut extra_env = Vec::new();
    if args.peek().is_some_and(|a| a == "--help" || a == "-h") {
        println!("Usage: trames-run [--rootfs VERIFIED_ALPINE_TAR_GZ] [--guest] [--env KEY=VALUE] [--virtual-network ID] [--host-map VIRTUAL_IP=HOST_IP] [--network off|loopback|outbound|full] [--storage DIR | --memory] [--storage-budget BYTES|16GiB|auto] [--storage-info] [--memory-budget 1GiB] [--process-memory-budget 512MiB] [--max-instructions N] [--trace-syscalls] [--metrics] [--cpus auto|1..32] [--accelerator pure|off] [--native-bsearch] [--native-allocator] [--wayland] [--vulkan] [--musl-libc HOST_ELF] [--file /GUEST=HOST] [--exec-file /GUEST=HOST] [--export /GUEST=HOST] [--install-busybox] [--pty] [--] ELF [ARG ...]\nDefault native network: full host TCP/UDP access and listeners, subject to host OS permissions; use --network outbound or loopback to restrict it.\n--virtual-network ID selects isolated 10.0.0.ID / fd00::ID and private loopback; ID must be 2..254.\n--host-map explicitly forwards a logical external IP to a trusted host service.\nDefault storage: .trames/storage in the current directory, 16 GiB logical file budget on first creation.\nDefault committed memory: 512 MiB per address space, 1 GiB aggregate; --process-memory-budget and --memory-budget change these limits without preallocation.\nImports copy selected host files; exports create new host files after guest exit.\n--memory selects the ephemeral 16 MiB test backend. auto uses the storage volume's total capacity.");
        return Ok(0);
    }
    while let Some(option) = args.peek() {
        match option.as_str() {
            "--cpus" => {
                args.next();
                let value = args.next().ok_or("missing CPU count")?;
                cpu_count = if value == "auto" {
                    None
                } else {
                    let n = value.parse::<usize>()?;
                    if !(1..=32).contains(&n) {
                        return Err("CPU count must be 1..32".into());
                    }
                    Some(n)
                };
            }
            "--rootfs" => {
                args.next();
                rootfs = Some(args.next().ok_or("missing rootfs archive")?);
            }
            "--guest" => {
                args.next();
                guest_path = true;
            }
            "--musl-libc" => {
                args.next();
                musl_libc = Some(args.next().ok_or("missing musl ELF")?);
            }
            "--accelerator" => {
                args.next();
                pure_accelerator = match args.next().as_deref() {
                    Some("pure") => true,
                    Some("off") => false,
                    _ => return Err("--accelerator supports pure or off".into()),
                };
            }
            "--native-bsearch" => {
                args.next();
                native_bsearch = true;
            }
            "--native-allocator" => {
                args.next();
                native_allocator = true;
            }
            "--wayland" => {
                args.next();
                wayland = true;
            }
            "--vulkan" => {
                args.next();
                vulkan_accelerator = true;
            }
            "--metrics" => {
                args.next();
                metrics = true;
            }
            "--env" => {
                args.next();
                let value = args.next().ok_or("missing environment assignment")?;
                let (name, _) = value
                    .split_once('=')
                    .ok_or("environment requires KEY=VALUE")?;
                if name.is_empty() || !name.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'_')
                {
                    return Err("invalid environment name".into());
                }
                extra_env.push(value);
            }
            "--virtual-network" => {
                args.next();
                virtual_network = Some(
                    args.next()
                        .ok_or("missing virtual environment ID")?
                        .parse::<u8>()?,
                );
            }
            "--host-map" => {
                args.next();
                let value = args.next().ok_or("missing host mapping")?;
                let (virtual_ip, host_ip) = value
                    .split_once('=')
                    .ok_or("host mapping requires VIRTUAL_IP=HOST_IP")?;
                host_mappings.push((
                    virtual_ip.parse::<std::net::IpAddr>()?,
                    host_ip.parse::<std::net::IpAddr>()?,
                ));
            }
            "--network" => {
                args.next();
                network = match args.next().ok_or("missing network mode")?.as_str() {
                    "off" => NetworkMode::Off,
                    "loopback" => NetworkMode::Loopback,
                    "outbound" => NetworkMode::Outbound,
                    "full" => NetworkMode::Full,
                    _ => return Err("network mode must be off, loopback, outbound or full".into()),
                };
            }
            "--memory" => {
                args.next();
                memory_only = true;
            }
            "--storage" => {
                args.next();
                storage = Some(args.next().ok_or("missing storage directory")?);
            }
            "--storage-budget" => {
                args.next();
                disk_budget = Some(args.next().ok_or("missing storage budget")?);
            }
            "--memory-budget" | "--process-memory-budget" => {
                let process = args.next().expect("peeked option") == "--process-memory-budget";
                let value = args.next().ok_or("missing memory budget")?;
                let bytes = usize::try_from(
                    storage_budget(&value).map_err(|e| format!("invalid memory budget: {e}"))?,
                )?;
                if process {
                    memory_limits.per_process = bytes;
                } else {
                    memory_limits.total = bytes;
                }
            }
            "--storage-info" => {
                args.next();
                storage_info = true;
            }
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
            "--file" | "--exec-file" | "--export" => {
                let executable = option == "--exec-file";
                let export = option == "--export";
                args.next();
                let mapping = args.next().ok_or("missing /GUEST=HOST mapping")?;
                let (guest, host) = mapping
                    .split_once('=')
                    .ok_or("file mapping requires /GUEST=HOST")?;
                if export {
                    exports.push((guest.to_string(), host.to_string()));
                } else {
                    files.push((guest.to_string(), host.to_string(), executable));
                }
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
    let mut envp = vec![
        "PATH=/usr/sbin:/usr/bin:/sbin:/bin".into(),
        "LANG=C".into(),
        "HOME=/root".into(),
        "USER=root".into(),
        "LOGNAME=root".into(),
    ];
    for value in extra_env {
        let name = value.split_once('=').expect("validated env").0;
        envp.retain(|e: &String| e.split_once('=').is_none_or(|(key, _)| key != name));
        envp.push(value);
    }
    memory_limits.validate()?;
    let mut vfs = Vfs::default();
    if memory_only && (storage.is_some() || disk_budget.is_some()) {
        return Err("--memory cannot be combined with disk options".into());
    }
    if !memory_only {
        let directory = storage.as_deref().unwrap_or(".trames/storage");
        vfs = Vfs::open_disk(directory, DiskOptions::default())?;
        let volume = volume_space(directory)?;
        if let Some(budget) = disk_budget {
            let file_bytes = if budget == "auto" {
                volume.total_bytes
            } else {
                storage_budget(&budget)?
            };
            vfs.set_storage_limits(StorageLimits {
                file_bytes,
                nodes: vfs.capacity().node_limit,
            })?;
        }
        vfs.set_volume_space(volume)?;
    }
    if let Some(id) = virtual_network {
        let fabric = trames_net::Fabric::new()?;
        for (virtual_ip, host_ip) in host_mappings {
            fabric.map_host_ip(virtual_ip, host_ip)?;
        }
        vfs.configure_virtual_network(&fabric, id, network)?;
    } else {
        vfs.configure_network(network)?;
        for (virtual_ip, host_ip) in host_mappings {
            vfs.map_network_host_ip(virtual_ip, host_ip)?;
        }
    }
    if let Some(archive) = rootfs {
        trames_distro::install_alpine(&mut File::open(archive)?, &mut vfs)?;
    }
    for (guest, host, executable) in files {
        vfs.import_reader(guest.as_bytes(), &mut File::open(host)?, executable)?;
    }
    if vfs
        .stat_at(-100, b"/etc/resolv.conf")
        .is_err_and(|e| e.0 == 2)
    {
        if vfs.stat_at(-100, b"/etc").is_err_and(|e| e.0 == 2) {
            vfs.mkdir_at(-100, b"/etc", 0o755)?;
        }
        vfs.import_reader(
            b"/etc/resolv.conf",
            &mut if virtual_network.is_some() {
                &b"nameserver 10.0.0.1\n"[..]
            } else {
                // Match iPad sessions: Docker filters loopback nameservers
                // from container resolv.conf. The managed resolver is reachable
                // from each guest network namespace, without a daemon override.
                &b"nameserver 169.254.0.53\n"[..]
            },
            false,
        )?;
    }
    if let Some(libc) = musl_libc {
        trames_distro::install_musl(&mut File::open(libc)?, &mut vfs)?;
    }
    if guest_path {
        if vfs.stat_at(-100, b"/root").is_err_and(|e| e.0 == 2) {
            vfs.mkdir_at(-100, b"/root", 0o700)?;
        }
        vfs.chdir(b"/root")?;
    }
    if wayland {
        let directory = envp
            .iter()
            .find_map(|s: &String| s.strip_prefix("XDG_RUNTIME_DIR="))
            .unwrap_or("/run/user/0")
            .to_owned();
        vfs.enable_wayland(directory.as_bytes())?;
        if !envp.iter().any(|s| s.starts_with("XDG_RUNTIME_DIR=")) {
            envp.push(format!("XDG_RUNTIME_DIR={directory}"));
        }
        if !envp.iter().any(|s| s.starts_with("WAYLAND_DISPLAY=")) {
            envp.push("WAYLAND_DISPLAY=wayland-0".into());
        }
    }
    let bytes = if guest_path {
        vfs.executable(path.as_bytes())?
    } else {
        let mut bytes = Vec::new();
        File::open(&path)?
            .take(MAX_ELF_SIZE as u64 + 1)
            .read_to_end(&mut bytes)?;
        if bytes.len() > MAX_ELF_SIZE {
            return Err("ELF exceeds 64 MiB file limit".into());
        }
        bytes
    };
    let mut runtime = Runtime::load_with_options(
        &bytes,
        &argv,
        &envp,
        vfs,
        trames_core::RuntimeOptions {
            memory_limits,
            pure_accelerator,
            native_bsearch,
            native_allocator,
            vulkan_accelerator,
        },
    )?;
    runtime.linux.trace = trace;
    if install_busybox {
        runtime
            .linux
            .vfs
            .install_executable(b"/bin/busybox", &bytes)?;
        for directory in [b"/usr".as_slice(), b"/usr/bin".as_slice()] {
            match runtime.linux.vfs.mkdir_at(-100, directory, 0o755) {
                Ok(()) | Err(trames_vfs::Errno(17)) => {}
                Err(e) => return Err(e.into()),
            }
        }
        for applet in [
            "sh", "ash", "busybox", "echo", "printf", "true", "false", "cat", "wc", "head", "tail",
            "pwd", "ls", "basename", "dirname", "sleep", "kill", "seq", "env", "test", "mkdir",
            "rm", "touch", "chmod", "cp", "mv", "ln", "sync", "dd",
        ] {
            for directory in ["/bin", "/usr/bin"] {
                let alias = format!("{directory}/{applet}");
                if alias != "/bin/busybox" {
                    let expected = runtime.linux.vfs.stat_at(-100, b"/bin/busybox")?;
                    match runtime.linux.vfs.stat_at(-100, alias.as_bytes()) {
                        Ok(stat) if stat[8..16] == expected[8..16] => {}
                        Ok(_) => {
                            return Err(format!(
                                "BusyBox alias already belongs to another inode: {alias}"
                            )
                            .into())
                        }
                        Err(trames_vfs::Errno(2)) => runtime.linux.vfs.import_alias(
                            b"/bin/busybox",
                            alias.as_bytes(),
                            true,
                        )?,
                        Err(e) => return Err(e.into()),
                    }
                }
            }
        }
    }
    if storage_info {
        eprintln!("trames storage: {:?}", runtime.linux.vfs.capacity());
    }
    if pty {
        runtime.enable_terminal(trames_core::Terminal::default())?;
    }
    let _host_terminal = RawTerminal::enter(pty)?;
    runtime.profile_syscalls = metrics;
    if let Some(count) = cpu_count {
        runtime.cpu_count = count;
    }
    let began = std::time::Instant::now();
    let result = runtime.run(&mut HostOutput::new(), budget);
    let synced = runtime.linux.vfs.sync();
    if metrics {
        let seconds = began.elapsed().as_secs_f64();
        let profile = &runtime.metrics;
        let snapshots = serde_json::json!({
            "cache_hits":profile.parallel_snapshots.cache_hits,"dirty_refreshes":profile.parallel_snapshots.dirty_refreshes,
            "full_rebuilds":profile.parallel_snapshots.full_rebuilds,"full_rebuild_pages":profile.parallel_snapshots.full_rebuild_pages,
            "backing_invalidations":profile.parallel_snapshots.backing_invalidations,"code_invalidations":profile.parallel_snapshots.code_invalidations,
            "source_page_reads":profile.parallel_snapshots.source_page_reads,"incremental_updates":profile.parallel_snapshots.incremental_updates,"incremental_pages":profile.parallel_snapshots.incremental_pages
        });
        eprintln!(
            "trames native metrics: {}",
            serde_json::to_string(&profile.native)?
        );
        eprintln!(
            "trames metrics: {}",
            serde_json::json!({
                "backend":backend,"instructions":profile.instructions,"seconds":seconds,
                "runtime_error":result.as_ref().err().map(ToString::to_string),
                "gpu_compiles":profile.gpu_compiles,"gpu_peer_instructions":profile.gpu_peer_instructions,
                "gpu_peak_pending":profile.gpu_peak_pending,
                "gpu_wait_seconds":profile.gpu_wait_time.as_secs_f64(),
                "gpu_upload_bytes":profile.gpu_transfers.upload_bytes,"gpu_download_bytes":profile.gpu_transfers.download_bytes,
                "gpu_direct_bytes":profile.gpu_transfers.direct_bytes,"gpu_buffered_bytes":profile.gpu_transfers.buffered_bytes,
                "instructions_per_second":profile.instructions as f64/seconds,
                "cpu_seconds":profile.cpu_time.as_secs_f64(),"syscall_seconds":profile.syscall_time.as_secs_f64(),
                "idle_seconds":profile.idle_time.as_secs_f64(),"syscalls":profile.syscalls,
                "proc_refreshes":profile.proc_refreshes,"context_switches":profile.context_switches,
                "decode_lookups":profile.decode_lookups,"decode_misses":profile.decode_misses,"instructions_decoded":profile.instructions_decoded,
                "cpu_count":profile.cpu_count,"parallel_rounds":profile.parallel_rounds,"worker_slices":profile.worker_slices,"worker_chunks":profile.worker_chunks,"worker_syscalls":profile.worker_syscalls,
                "worker_direct_syscalls":profile.worker_direct_syscalls,
                "worker_futex_wakes":profile.worker_futex_wakes,
                "worker_futex_woken":profile.worker_futex_woken,
                "worker_futex_requeues":profile.worker_futex_requeues,
                "worker_futex_waits":profile.worker_futex_waits,
                "worker_task_resumes":profile.worker_task_resumes,
                "worker_task_yields":profile.worker_task_yields,
                "worker_io_waits":profile.worker_io_waits,
                "worker_service_waits":profile.worker_service_waits,
                "worker_file_syscalls":profile.worker_file_syscalls,
                "worker_socket_syscalls":profile.worker_socket_syscalls,
                "worker_timer_syscalls":profile.worker_timer_syscalls,
                "worker_poll_syscalls":profile.worker_poll_syscalls,
                "worker_uring_syscalls":profile.worker_uring_syscalls,
                "worker_fd_syscalls":profile.worker_fd_syscalls,
                "worker_gpu_calls":profile.worker_gpu_calls,
                "worker_gpu_waits":profile.worker_gpu_waits,
                "disk_reads":profile.disk_reads,"peak_disk_reads":profile.peak_disk_reads,"disk_read_bytes":profile.disk_read_bytes,
                "service_requests":profile.service_requests,"serviced_syscalls":profile.serviced_syscalls,"service_wait_seconds":profile.service_wait_time.as_secs_f64(),
                "dispatch_seconds":profile.parallel_dispatch_time.as_secs_f64(),"stop_seconds":profile.parallel_stop_time.as_secs_f64(),
                "peak_parallel_cpus":profile.peak_parallel_cpus,"peak_parallel_native_calls":profile.peak_parallel_native_calls,"memory_seconds":profile.parallel_memory_time.as_secs_f64(),
                "parallel_snapshots":snapshots
            })
        );
    }
    let exit = result?;
    synced?;
    for (guest, host) in exports {
        let stat = runtime.linux.vfs.stat_at(-100, guest.as_bytes())?;
        let mode = u32::from_le_bytes(stat[16..20].try_into().expect("stat mode"));
        if mode & 0o170000 != 0o100000 || mode & 0o400 == 0 {
            return Err("export requires a readable regular guest file".into());
        }
        let mut file = File::options().write(true).create_new(true).open(host)?;
        runtime
            .linux
            .vfs
            .export_writer(guest.as_bytes(), &mut file)?;
        file.sync_all()?;
    }
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
