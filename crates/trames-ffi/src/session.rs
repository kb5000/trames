// SPDX-License-Identifier: GPL-3.0-or-later
#![forbid(unsafe_code)]
use serde::{Deserialize, Serialize};
use std::{
    collections::VecDeque,
    fs::{self, File, OpenOptions},
    io::{self, Read},
    path::PathBuf,
    sync::{
        atomic::{AtomicBool, AtomicUsize, Ordering},
        mpsc, Arc, Mutex,
    },
    time::Duration,
};
use trames_core::{Runtime, RuntimeError, Terminal};
use trames_linux::{GuestOutput, OutputFd, TerminalEvent};
use trames_vfs::{DiskOptions, NetworkMode, Vfs, VolumeSpace};

pub const QUEUE_LIMIT: usize = 65536;
static WORKERS: AtomicUsize = AtomicUsize::new(0);
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Options {
    pub store: PathBuf,
    pub rootfs: Option<PathBuf>,
    pub volume_total: u64,
    pub volume_available: u64,
    #[serde(default = "default_rows")]
    pub rows: u16,
    #[serde(default = "default_columns")]
    pub columns: u16,
    #[serde(default = "default_network")]
    pub network: String,
    pub budget_bytes: Option<u64>,
}
pub fn install(options: Options) -> Result<(), String> {
    if !options.store.is_absolute() || options.store.exists() {
        return Err("installation requires an absent absolute staging directory".into());
    }
    let archive = options.rootfs.ok_or("rootfs archive required")?;
    let mut vfs = Vfs::open_disk(
        &options.store,
        DiskOptions {
            volume_space: Some(VolumeSpace {
                total_bytes: options.volume_total,
                available_bytes: options.volume_available,
            }),
            ..DiskOptions::default()
        },
    )
    .map_err(|e| e.to_string())?;
    trames_distro::install_alpine(
        &mut File::open(archive).map_err(|e| e.to_string())?,
        &mut vfs,
    )
    .map_err(|e| e.to_string())?;
    vfs.executable(b"/bin/ash").map_err(|e| e.to_string())?;
    Ok(())
}
fn default_rows() -> u16 {
    24
}
fn default_columns() -> u16 {
    80
}
fn default_network() -> String {
    "outbound".into()
}
#[derive(Clone, Serialize)]
pub struct Status {
    pub phase: &'static str,
    pub error: Option<String>,
    pub exit_code: Option<u8>,
    pub budget_bytes: u64,
    pub used_bytes: u64,
    pub available_bytes: u64,
    pub volume_total_bytes: u64,
}
pub struct Buffers {
    pub input: VecDeque<u8>,
    pub output: VecDeque<u8>,
    pub size: Option<(u16, u16)>,
    pub status: Status,
}
pub struct Session {
    pub buffers: Mutex<Buffers>,
    pub cancelled: AtomicBool,
    pub paused: AtomicBool,
    commands: mpsc::SyncSender<Transfer>,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct TransferOptions {
    pub guest: String,
    pub host: PathBuf,
    pub export: bool,
}
struct Transfer {
    options: TransferOptions,
    result: mpsc::SyncSender<Result<(), String>>,
}
struct WorkerPermit;
impl Drop for WorkerPermit {
    fn drop(&mut self) {
        WORKERS.fetch_sub(1, Ordering::AcqRel);
    }
}
impl Session {
    pub fn start(options: Options) -> Result<Arc<Self>, String> {
        if !options.store.is_absolute()
            || options.store.as_os_str().len() > 4096
            || options
                .rootfs
                .as_ref()
                .is_some_and(|p| !p.is_absolute() || p.as_os_str().len() > 4096)
            || options.rows == 0
            || options.columns == 0
            || options.rows > 4096
            || options.columns > 4096
            || options.volume_total == 0
            || options.volume_available > options.volume_total
            || !matches!(options.network.as_str(), "off" | "loopback" | "outbound")
        {
            return Err("invalid session configuration".into());
        }
        WORKERS
            .fetch_update(Ordering::AcqRel, Ordering::Acquire, |n| {
                (n < 8).then_some(n + 1)
            })
            .map_err(|_| "session worker limit reached")?;
        let permit = WorkerPermit;
        let (commands, receiver) = mpsc::sync_channel(8);
        let session = Arc::new(Self {
            buffers: Mutex::new(Buffers {
                input: VecDeque::new(),
                output: VecDeque::new(),
                size: None,
                status: Status {
                    phase: "starting",
                    error: None,
                    exit_code: None,
                    budget_bytes: 0,
                    used_bytes: 0,
                    available_bytes: 0,
                    volume_total_bytes: options.volume_total,
                },
            }),
            cancelled: AtomicBool::new(false),
            paused: AtomicBool::new(false),
            commands,
        });
        let worker_session = session.clone();
        std::thread::Builder::new()
            .name("trames-local".into())
            .spawn(move || {
                let _permit = permit;
                let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                    run(options, worker_session.clone(), receiver)
                }));
                let mut buffers = worker_session
                    .buffers
                    .lock()
                    .unwrap_or_else(|p| p.into_inner());
                match result {
                    Ok(Ok(code)) => {
                        buffers.status.phase = "exited";
                        buffers.status.exit_code = Some(code);
                    }
                    _ if worker_session.cancelled.load(Ordering::Acquire) => {
                        buffers.status.phase = "cancelled"
                    }
                    Ok(Err(error)) => {
                        buffers.status.phase = "failed";
                        buffers.status.error = Some(error.chars().take(4096).collect());
                    }
                    Err(_) => {
                        buffers.status.phase = "failed";
                        buffers.status.error = Some("local runtime panicked".into());
                    }
                }
            })
            .map_err(|e| e.to_string())?;
        Ok(session)
    }
    pub fn transfer(&self, options: TransferOptions) -> Result<(), String> {
        if !options.guest.starts_with('/')
            || options.guest.len() > 4096
            || options.guest.contains('\0')
            || !options.host.is_absolute()
            || options.host.as_os_str().len() > 4096
        {
            return Err("invalid transfer path".into());
        }
        let (result, response) = mpsc::sync_channel(1);
        self.commands
            .try_send(Transfer { options, result })
            .map_err(|_| "local session is busy or closed")?;
        loop {
            if self.cancelled.load(Ordering::Acquire) {
                return Err("session cancelled".into());
            }
            match response.recv_timeout(Duration::from_millis(50)) {
                Ok(result) => return result,
                Err(mpsc::RecvTimeoutError::Timeout) => {}
                Err(_) => return Err("local session closed".into()),
            }
        }
    }
}
fn run(
    options: Options,
    session: Arc<Session>,
    commands: mpsc::Receiver<Transfer>,
) -> Result<u8, String> {
    let mut vfs = Vfs::open_disk(
        &options.store,
        DiskOptions {
            volume_space: Some(VolumeSpace {
                total_bytes: options.volume_total,
                available_bytes: options.volume_available,
            }),
            ..DiskOptions::default()
        },
    )
    .map_err(|e| e.to_string())?;
    if let Some(budget) = options.budget_bytes {
        let mut limits = trames_vfs::StorageLimits::disk();
        limits.file_bytes = budget;
        vfs.set_storage_limits(limits).map_err(|e| e.to_string())?;
    }
    // First installation uses a new store chosen by the native adapter. A partial
    // failed installation is never promoted to the active environment directory.
    if vfs.executable(b"/bin/ash").is_err() {
        let archive = options
            .rootfs
            .ok_or("Alpine rootfs archive is required for first installation")?;
        trames_distro::install_alpine(
            &mut File::open(archive).map_err(|e| e.to_string())?,
            &mut vfs,
        )
        .map_err(|e| e.to_string())?;
    }
    if session.cancelled.load(Ordering::Acquire) {
        return Err("session cancelled".into());
    }
    for (path, bytes) in [
        (b"/home/user/.trames".as_slice(), b"Trames\n".as_slice()),
        (
            b"/etc/resolv.conf".as_slice(),
            b"nameserver 1.1.1.1\nnameserver 2606:4700:4700::1111\n".as_slice(),
        ),
    ] {
        if vfs.stat_at(trames_vfs::AT_FDCWD, path).is_err() {
            vfs.import_file(path, bytes.to_vec())
                .map_err(|e| e.to_string())?;
        }
    }
    vfs.chdir(b"/home/user").map_err(|e| e.to_string())?;
    vfs.configure_network(match options.network.as_str() {
        "off" => NetworkMode::Off,
        "loopback" => NetworkMode::Loopback,
        _ => NetworkMode::Outbound,
    })
    .map_err(|e| e.to_string())?;
    let bytes = vfs.executable(b"/bin/ash").map_err(|e| e.to_string())?;
    let mut runtime = Runtime::load_with_vfs(
        &bytes,
        &["/bin/ash".into(), "-i".into()],
        &[
            "HOME=/home/user".into(),
            "USER=user".into(),
            "PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin".into(),
            "TERM=xterm-256color".into(),
            "LANG=C.UTF-8".into(),
        ],
        vfs,
    )
    .map_err(|e| e.to_string())?;
    let mut terminal = Terminal::default();
    terminal.resize(options.rows, options.columns);
    runtime
        .enable_terminal(terminal)
        .map_err(|e| e.to_string())?;
    let mut io = Adapter {
        session: session.clone(),
        commands,
    };
    io.snapshot(&runtime.linux.vfs);
    session
        .buffers
        .lock()
        .map_err(|_| "session lock failed")?
        .status
        .phase = "running";
    let result = runtime.run(&mut io, u64::MAX);
    runtime.linux.vfs.finish_network();
    runtime.linux.vfs.close_all();
    io.snapshot(&runtime.linux.vfs);
    result.map(|e| e.code).map_err(|e| match e {
        RuntimeError::Cancelled => "session cancelled".into(),
        _ => e.to_string(),
    })
}
struct Adapter {
    session: Arc<Session>,
    commands: mpsc::Receiver<Transfer>,
}
impl Adapter {
    fn snapshot(&self, vfs: &Vfs) {
        let capacity = vfs.capacity();
        let mut b = self
            .session
            .buffers
            .lock()
            .unwrap_or_else(|p| p.into_inner());
        b.status.budget_bytes = capacity.budget_bytes;
        b.status.used_bytes = capacity.used_bytes;
        b.status.available_bytes = capacity.available_bytes;
    }
    fn transfer(&self, vfs: &mut Vfs, o: TransferOptions) -> Result<(), String> {
        if o.export {
            let mut file = OpenOptions::new()
                .write(true)
                .create_new(true)
                .open(&o.host)
                .map_err(|e| e.to_string())?;
            let result = vfs
                .export_writer(o.guest.as_bytes(), &mut file)
                .map_err(|e| e.to_string());
            if result.is_err() {
                let _ = fs::remove_file(o.host);
            }
            result.map(|_| ())
        } else {
            let file = File::open(o.host).map_err(|e| e.to_string())?;
            vfs.import_reader(
                o.guest.as_bytes(),
                &mut CancelReader {
                    file,
                    cancelled: &self.session.cancelled,
                },
                false,
            )
            .map(|_| ())
            .map_err(|e| e.to_string())
        }
    }
}
struct CancelReader<'a> {
    file: File,
    cancelled: &'a AtomicBool,
}
impl Read for CancelReader<'_> {
    fn read(&mut self, bytes: &mut [u8]) -> io::Result<usize> {
        if self.cancelled.load(Ordering::Acquire) {
            return Err(io::ErrorKind::Interrupted.into());
        }
        self.file.read(bytes)
    }
}
impl GuestOutput for Adapter {
    fn control(&mut self, vfs: &mut Vfs) -> io::Result<bool> {
        if self.session.cancelled.load(Ordering::Acquire) {
            return Ok(false);
        }
        if let Ok(command) = self.commands.try_recv() {
            let result = self.transfer(vfs, command.options);
            self.snapshot(vfs);
            let _ = command.result.send(result);
        }
        while self.session.paused.load(Ordering::Acquire) {
            if self.session.cancelled.load(Ordering::Acquire) {
                return Ok(false);
            }
            std::thread::sleep(Duration::from_millis(10));
        }
        Ok(true)
    }
    fn write(&mut self, _: OutputFd, bytes: &[u8]) -> io::Result<usize> {
        let mut b = self
            .session
            .buffers
            .lock()
            .map_err(|_| io::Error::other("session lock failed"))?;
        let count = bytes.len().min(QUEUE_LIMIT - b.output.len());
        if count == 0 && !bytes.is_empty() {
            return Err(io::ErrorKind::WouldBlock.into());
        }
        b.output.extend(&bytes[..count]);
        Ok(count)
    }
    fn poll_stdin(&mut self, bytes: &mut [u8]) -> io::Result<usize> {
        let mut b = self
            .session
            .buffers
            .lock()
            .map_err(|_| io::Error::other("session lock failed"))?;
        if b.input.is_empty() {
            return Err(io::ErrorKind::WouldBlock.into());
        }
        let count = bytes.len().min(b.input.len());
        for byte in &mut bytes[..count] {
            *byte = b.input.pop_front().expect("bounded input");
        }
        Ok(count)
    }
    fn stdin_ready(&mut self) -> io::Result<bool> {
        Ok(!self
            .session
            .buffers
            .lock()
            .map_err(|_| io::Error::other("session lock failed"))?
            .input
            .is_empty())
    }
    fn terminal_event(&mut self) -> Option<TerminalEvent> {
        self.session
            .buffers
            .lock()
            .ok()?
            .size
            .take()
            .map(|(rows, columns)| TerminalEvent::Resize { rows, columns })
    }
}
