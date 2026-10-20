// SPDX-License-Identifier: GPL-3.0-or-later
#![deny(unsafe_op_in_unsafe_fn)]
use serde::{Deserialize, Serialize};
use std::{
    collections::BTreeMap,
    fs::{self, File, OpenOptions},
    io::{self, Read, Write},
    path::PathBuf,
    sync::{
        atomic::{AtomicBool, AtomicU64, AtomicUsize, Ordering},
        mpsc, Arc, Mutex,
    },
    time::{Duration, Instant},
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
    pub musl_libc: Option<PathBuf>,
    pub volume_total: u64,
    pub volume_available: u64,
    #[serde(default = "default_rows")]
    pub rows: u16,
    #[serde(default = "default_columns")]
    pub columns: u16,
    #[serde(default = "default_network")]
    pub network: String,
    pub budget_bytes: Option<u64>,
    pub memory_budget_bytes: Option<usize>,
    pub process_memory_budget_bytes: Option<usize>,
    #[serde(default)]
    pub pure_accelerator: bool,
    #[serde(default)]
    pub native_bsearch: bool,
    #[serde(default)]
    pub native_allocator: bool,
    #[serde(default)]
    pub vulkan_accelerator: bool,
    #[serde(default)]
    pub wayland: bool,
    pub xwayland_launcher: Option<PathBuf>,
    #[serde(default)]
    pub multiple_terminals: bool,
    pub vulkan_icd: Option<PathBuf>,
    pub timezone: Option<HostTimezone>,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct HostTimezone {
    pub identifier: String,
    pub data: Vec<u8>,
}
enum Command {
    Transfer(Transfer),
    Timezone(HostTimezone, mpsc::SyncSender<Result<(), String>>),
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
    if let Some(libc) = options.musl_libc {
        trames_distro::install_musl(&mut File::open(libc).map_err(|e| e.to_string())?, &mut vfs)
            .map_err(|e| e.to_string())?;
    }
    vfs.executable(b"/bin/ash").map_err(|e| e.to_string())?;
    Ok(())
}
fn memory_limits(options: &Options) -> Result<trames_core::MemoryLimits, String> {
    let defaults = trames_core::MemoryLimits::default();
    let limits = trames_core::MemoryLimits {
        total: options.memory_budget_bytes.unwrap_or(defaults.total),
        per_process: options
            .process_memory_budget_bytes
            .unwrap_or(defaults.per_process),
    };
    limits.validate().map_err(|e| e.to_string())?;
    Ok(limits)
}
fn default_rows() -> u16 {
    24
}

fn default_columns() -> u16 {
    80
}
fn default_network() -> String {
    "full".into()
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
    pub node_limit: usize,
    pub nodes_used: usize,
}
pub struct Buffers {
    pub(crate) terminals: BTreeMap<u64, crate::terminals::Channel>,
    pub status: Status,
}
pub struct Session {
    pub display: trames_core::VulkanDisplay,
    pub wayland: Mutex<Option<trames_wayland::Server>>,
    pub bridge: trames_bridge::Broker,
    pub buffers: Mutex<Buffers>,
    pub cancelled: AtomicBool,
    pub paused: AtomicBool,
    pub multiple_terminals: bool,
    pub next_terminal: AtomicU64,
    commands: mpsc::SyncSender<Command>,
    transfers: Mutex<BTreeMap<u64, TransferTicket>>,
    next_transfer: AtomicU64,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct TransferOptions {
    pub guest: String,
    pub host: PathBuf,
    pub export: bool,
    #[serde(default)]
    pub token: u64,
    pub maximum_bytes: Option<u64>,
    #[serde(default)]
    pub replace: bool,
}
struct Transfer {
    options: TransferOptions,
    result: mpsc::SyncSender<Result<(), String>>,
    cancelled: Arc<AtomicBool>,
}
struct TransferTicket {
    cancelled: Arc<AtomicBool>,
    claimed: bool,
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
            || options
                .musl_libc
                .as_ref()
                .is_some_and(|p| !p.is_absolute() || p.as_os_str().len() > 4096)
            || options.rows == 0
            || options.columns == 0
            || options.rows > 4096
            || options.columns > 4096
            || options.volume_total == 0
            || options.volume_available > options.volume_total
            || !matches!(
                options.network.as_str(),
                "off" | "loopback" | "outbound" | "full"
            )
        {
            return Err("invalid session configuration".into());
        }
        memory_limits(&options)?;
        WORKERS
            .try_update(Ordering::AcqRel, Ordering::Acquire, |n| {
                (n < 8).then_some(n + 1)
            })
            .map_err(|_| "session worker limit reached")?;
        let permit = WorkerPermit;
        let (commands, receiver) = mpsc::sync_channel(8);
        let session = Arc::new(Self {
            display: trames_core::VulkanDisplay::default(),
            wayland: Mutex::new(None),
            bridge: trames_bridge::Broker::default(),
            buffers: Mutex::new(Buffers {
                terminals: BTreeMap::from([(
                    1,
                    crate::terminals::Channel {
                        size: Some((options.rows, options.columns)),
                        ..Default::default()
                    },
                )]),
                status: Status {
                    phase: "starting",
                    error: None,
                    exit_code: None,
                    budget_bytes: 0,
                    used_bytes: 0,
                    available_bytes: 0,
                    volume_total_bytes: options.volume_total,
                    node_limit: 0,
                    nodes_used: 0,
                },
            }),
            cancelled: AtomicBool::new(false),
            paused: AtomicBool::new(false),
            multiple_terminals: options.multiple_terminals,
            next_terminal: AtomicU64::new(2),
            commands,
            transfers: Mutex::new(BTreeMap::new()),
            next_transfer: AtomicU64::new(1),
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
    pub fn begin_transfer(&self) -> Result<u64, String> {
        let mut transfers = self.transfers.lock().map_err(|_| "transfer lock failed")?;
        if transfers.len() >= 8 {
            return Err("transfer limit reached".into());
        }
        let token = self
            .next_transfer
            .try_update(Ordering::AcqRel, Ordering::Acquire, |n| n.checked_add(1))
            .map_err(|_| "transfer tokens exhausted")?;
        transfers.insert(
            token,
            TransferTicket {
                cancelled: Arc::new(AtomicBool::new(false)),
                claimed: false,
            },
        );
        Ok(token)
    }
    pub fn synchronize_timezone(&self, timezone: HostTimezone) -> Result<(), String> {
        let (response, receiver) = mpsc::sync_channel(1);
        self.commands
            .try_send(Command::Timezone(timezone, response))
            .map_err(|_| "local session is busy or closed")?;
        loop {
            if self.cancelled.load(Ordering::Acquire) {
                return Err("session cancelled".into());
            }
            match receiver.recv_timeout(Duration::from_millis(50)) {
                Ok(result) => return result,
                Err(mpsc::RecvTimeoutError::Timeout) => {}
                Err(_) => return Err("local session closed".into()),
            }
        }
    }
    pub fn cancel_transfer(&self, token: u64) -> Result<(), String> {
        let transfers = self.transfers.lock().map_err(|_| "transfer lock failed")?;
        let flag = transfers.get(&token).ok_or("invalid transfer token")?;
        flag.cancelled.store(true, Ordering::Release);
        Ok(())
    }
    pub fn transfer(&self, options: TransferOptions) -> Result<(), String> {
        let token = options.token;
        let flag = if token == 0 {
            Arc::new(AtomicBool::new(false))
        } else {
            let mut transfers = self.transfers.lock().map_err(|_| "transfer lock failed")?;
            let ticket = transfers.get_mut(&token).ok_or("invalid transfer token")?;
            if ticket.claimed {
                return Err("transfer token already used".into());
            }
            ticket.claimed = true;
            ticket.cancelled.clone()
        };
        let result = self.transfer_inner(options, flag);
        if token != 0 {
            self.transfers
                .lock()
                .map_err(|_| "transfer lock failed")?
                .remove(&token);
        }
        result
    }
    fn transfer_inner(
        &self,
        options: TransferOptions,
        cancelled: Arc<AtomicBool>,
    ) -> Result<(), String> {
        if cancelled.load(Ordering::Acquire) {
            return Err("transfer cancelled".into());
        }
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
            .try_send(Command::Transfer(Transfer {
                options,
                result,
                cancelled: cancelled.clone(),
            }))
            .map_err(|_| "local session is busy or closed")?;
        loop {
            if self.cancelled.load(Ordering::Acquire) || cancelled.load(Ordering::Acquire) {
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
fn prepare_runtime_mounts(vfs: &mut Vfs) -> Result<(), String> {
    let prepare = |vfs: &mut Vfs| -> Result<(), trames_vfs::Errno> {
        match vfs.mkdir_at(trames_vfs::AT_FDCWD, b"/dev", 0o755) {
            Ok(()) | Err(trames_vfs::Errno(17)) => {}
            Err(error) => return Err(error),
        }
        for (path, parent, mode, options) in [
            (
                b"/run".as_slice(),
                b"/".as_slice(),
                0o755,
                b"mode=755".as_slice(),
            ),
            (
                b"/dev/shm".as_slice(),
                b"/dev".as_slice(),
                0o1777,
                b"size=64m,mode=1777".as_slice(),
            ),
        ] {
            match vfs.mkdir_at(trames_vfs::AT_FDCWD, path, mode) {
                Ok(()) | Err(trames_vfs::Errno(17)) => {}
                Err(error) => return Err(error),
            }
            if vfs.mount_id_at(trames_vfs::AT_FDCWD, path, false)?
                == vfs.mount_id_at(trames_vfs::AT_FDCWD, parent, false)?
            {
                vfs.mount(Some(b"tmpfs"), path, Some(b"tmpfs"), 0, Some(options))?;
            }
        }
        Ok(())
    };
    prepare(vfs).map_err(|error| format!("prepare runtime tmpfs: {error}"))
}
fn prepare_guest_defaults(vfs: &mut Vfs, network: &str) -> Result<(), String> {
    let resolver = if network == "loopback" {
        b"nameserver 127.0.0.53\n".as_slice()
    } else {
        b"nameserver 169.254.0.53\n".as_slice()
    };
    for (path, bytes) in [
        (b"/root/.trames".as_slice(), b"Trames\n".as_slice()),
        (b"/etc/resolv.conf".as_slice(), resolver),
    ] {
        let old_managed_dns = path == b"/etc/resolv.conf"
            && vfs
                .open_at(trames_vfs::AT_FDCWD, path, 0, 0)
                .is_ok_and(|fd| {
                    let data = vfs.read(fd, 256).unwrap_or_default();
                    let _ = vfs.close(fd);
                    data != bytes
                        && matches!(
                            data.as_slice(),
                            b"nameserver 10.0.0.1\nnameserver fd00::1\n"
                                | b"nameserver 10.0.0.1\n"
                                | b"nameserver 127.0.0.53\n"
                                | b"nameserver 169.254.0.53\n"
                        )
                });
        if old_managed_dns {
            let fd = vfs
                .open_at(trames_vfs::AT_FDCWD, path, 1, 0)
                .map_err(|e| e.to_string())?;
            let result = vfs.write(fd, bytes).and_then(|written| {
                if written != bytes.len() {
                    return Err(trames_vfs::Errno(5));
                }
                vfs.truncate_fd(fd, bytes.len() as u64)
            });
            let close = vfs.close(fd);
            result.and(close).map_err(|e| e.to_string())?;
        } else if vfs.stat_at(trames_vfs::AT_FDCWD, path).is_err() {
            vfs.import_file(path, bytes.to_vec())
                .map_err(|e| e.to_string())?;
        }
    }
    Ok(())
}
fn run(
    options: Options,
    session: Arc<Session>,
    commands: mpsc::Receiver<Command>,
) -> Result<u8, String> {
    let guest_memory_limits = memory_limits(&options)?;
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
    vfs.attach_bridge(session.bridge.clone());
    if vfs
        .stat_at(trames_vfs::AT_FDCWD, b"/usr/local/bin/trames")
        .is_err()
    {
        vfs.import_executable(
            b"/usr/local/bin/trames",
            include_bytes!("../../../resources/guest/trames").to_vec(),
        )
        .map_err(|e| e.to_string())?;
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
    if let Some(libc) = options.musl_libc {
        trames_distro::install_musl(&mut File::open(libc).map_err(|e| e.to_string())?, &mut vfs)
            .map_err(|e| e.to_string())?;
    }
    // Before Wayland publishes its socket or any terminal/service can run.
    // Mounts belong to this kernel session, shared by all its local terminals.
    prepare_runtime_mounts(&mut vfs)?;
    prepare_guest_defaults(&mut vfs, &options.network)?;
    vfs.chdir(b"/root").map_err(|e| e.to_string())?;
    let mut zink_egl = false;
    if options.vulkan_accelerator {
        if let Some(path) = &options.vulkan_icd {
            trames_distro::install_vulkan_icd(
                &mut File::open(path).map_err(|e| e.to_string())?,
                &mut vfs,
            )
            .map_err(|e| format!("install Vulkan ICD: {e}"))?;
            match File::open(path.with_file_name("libEGL.so.1")) {
                Ok(mut reader) => {
                    trames_distro::install_zink_egl(&mut reader, &mut vfs)
                        .map_err(|e| format!("install Mesa EGL: {e}"))?;
                    zink_egl = true;
                }
                Err(e) if e.kind() == io::ErrorKind::NotFound => {}
                Err(e) => return Err(format!("read bundled Mesa EGL: {e}")),
            }
        }
    }
    if let Some(timezone) = options.timezone {
        trames_distro::synchronize_timezone(&mut vfs, &timezone.identifier, &timezone.data)
            .map_err(|e| e.to_string())?;
    }
    vfs.configure_network(match options.network.as_str() {
        "off" => NetworkMode::Off,
        "loopback" => NetworkMode::Loopback,
        "outbound" => NetworkMode::Outbound,
        "full" => NetworkMode::Full,
        _ => return Err("invalid network mode".into()),
    })
    .map_err(|e| e.to_string())?;
    let mut environment: Vec<String> = vec![
        "HOME=/root".into(),
        "USER=root".into(),
        "LOGNAME=root".into(),
        "PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin".into(),
        "TERM=xterm-256color".into(),
        "LANG=C.UTF-8".into(),
    ];
    if zink_egl {
        environment.extend([
            "GALLIUM_DRIVER=zink".into(),
            "MESA_LOADER_DRIVER_OVERRIDE=zink".into(),
            "LIBGL_KOPPER_WAYLAND_NO_DRM=true".into(),
            "mesa_glthread=false".into(),
            "VK_DRIVER_FILES=/usr/share/vulkan/icd.d/trames.json".into(),
        ]);
    }
    if options.wayland {
        if let Some(path) = options.xwayland_launcher {
            trames_distro::install_xwayland_launcher(
                &mut File::open(path).map_err(|e| e.to_string())?,
                &mut vfs,
            )
            .map_err(|e| e.to_string())?;
        }
        let server = vfs
            .enable_wayland(b"/run/user/0")
            .map_err(|e| e.to_string())?;
        server
            .attach_clipboard(Arc::new(crate::clipboard::Clipboard::new(&session)))
            .map_err(|e| e.to_string())?;
        *session
            .wayland
            .lock()
            .map_err(|_| "Wayland lock unavailable")? = Some(server);
        environment.extend([
            "XDG_RUNTIME_DIR=/run/user/0".into(),
            "WAYLAND_DISPLAY=wayland-0".into(),
            "DISPLAY=:0".into(),
        ]);
    }
    let bytes = vfs.executable(b"/bin/ash").map_err(|e| e.to_string())?;
    let argv = if options.multiple_terminals {
        vec![
            "/bin/ash".into(),
            "-c".into(),
            crate::terminals::INIT.into(),
        ]
    } else {
        vec!["/bin/ash".into(), "-i".into()]
    };
    let mut runtime = Runtime::load_with_options(
        &bytes,
        &argv,
        &environment,
        vfs,
        trames_core::RuntimeOptions {
            memory_limits: guest_memory_limits,
            pure_accelerator: options.pure_accelerator,
            native_bsearch: options.native_bsearch,
            native_allocator: options.native_allocator,
            vulkan_accelerator: options.vulkan_accelerator,
        },
    )
    .map_err(|e| e.to_string())?;
    let mut terminal = Terminal::default();
    runtime.vulkan_display = session.display.clone();
    terminal.resize(options.rows, options.columns);
    runtime
        .enable_terminal(terminal)
        .map_err(|e| e.to_string())?;
    let mut io = Adapter {
        session: session.clone(),
        commands,
        snapshot_at: Instant::now(),
        terminals: None,
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
    commands: mpsc::Receiver<Command>,
    snapshot_at: Instant,
    terminals: Option<crate::terminals::NativeTerminals>,
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
        b.status.node_limit = capacity.node_limit;
        b.status.nodes_used = capacity.nodes_used;
    }
    fn transfer(
        &self,
        vfs: &mut Vfs,
        o: TransferOptions,
        cancelled: &AtomicBool,
    ) -> Result<(), String> {
        if cancelled.load(Ordering::Acquire) || self.session.cancelled.load(Ordering::Acquire) {
            return Err("transfer cancelled".into());
        }
        if o.export {
            if let Some(maximum) = o.maximum_bytes {
                let stat = vfs
                    .stat_at(trames_vfs::AT_FDCWD, o.guest.as_bytes())
                    .map_err(|e| e.to_string())?;
                let size = u64::from_le_bytes(stat[48..56].try_into().expect("stat size"));
                if size > maximum {
                    return Err("guest file exceeds transfer size limit".into());
                }
            }
            let mut file = OpenOptions::new()
                .write(true)
                .create_new(true)
                .open(&o.host)
                .map_err(|e| e.to_string())?;
            let result = vfs
                .export_writer(
                    o.guest.as_bytes(),
                    &mut CancelWriter {
                        file: &mut file,
                        session: &self.session.cancelled,
                        cancelled,
                    },
                )
                .map_err(|e| e.to_string());
            if result.is_err() {
                let _ = fs::remove_file(o.host);
            }
            result.map(|_| ())
        } else {
            let file = File::open(o.host).map_err(|e| e.to_string())?;
            let metadata = file.metadata().map_err(|e| e.to_string())?;
            if !metadata.is_file()
                || o.maximum_bytes
                    .is_some_and(|maximum| metadata.len() > maximum)
            {
                return Err("source is not a regular file or exceeds transfer size limit".into());
            }
            let mut reader = CancelReader {
                file,
                session: &self.session.cancelled,
                cancelled,
                remaining: o.maximum_bytes.unwrap_or(u64::MAX),
            };
            (if o.replace {
                vfs.import_reader_replace(o.guest.as_bytes(), &mut reader)
            } else {
                vfs.import_reader(o.guest.as_bytes(), &mut reader, false)
            })
            .map(|_| ())
            .map_err(|e| e.to_string())
        }
    }
}
struct CancelReader<'a> {
    file: File,
    session: &'a AtomicBool,
    cancelled: &'a AtomicBool,
    remaining: u64,
}
impl Read for CancelReader<'_> {
    fn read(&mut self, bytes: &mut [u8]) -> io::Result<usize> {
        if self.cancelled.load(Ordering::Acquire) || self.session.load(Ordering::Acquire) {
            return Err(io::ErrorKind::Interrupted.into());
        }
        if bytes.is_empty() {
            return Ok(0);
        }
        if self.remaining == 0 {
            let mut extra = [0];
            return match self.file.read(&mut extra)? {
                0 => Ok(0),
                _ => Err(io::Error::new(
                    io::ErrorKind::FileTooLarge,
                    "source exceeds transfer size limit",
                )),
            };
        }
        let length = bytes
            .len()
            .min(self.remaining.min(usize::MAX as u64) as usize);
        let count = self.file.read(&mut bytes[..length])?;
        self.remaining -= count as u64;
        Ok(count)
    }
}
struct CancelWriter<'a> {
    file: &'a mut File,
    session: &'a AtomicBool,
    cancelled: &'a AtomicBool,
}
impl Write for CancelWriter<'_> {
    fn write(&mut self, bytes: &[u8]) -> io::Result<usize> {
        if self.cancelled.load(Ordering::Acquire) || self.session.load(Ordering::Acquire) {
            return Err(io::ErrorKind::Interrupted.into());
        }
        self.file.write(bytes)
    }
    fn flush(&mut self) -> io::Result<()> {
        self.file.flush()
    }
}
impl GuestOutput for Adapter {
    fn control(&mut self, vfs: &mut Vfs) -> io::Result<bool> {
        if self.session.cancelled.load(Ordering::Acquire) {
            return Ok(false);
        }
        if self.session.multiple_terminals {
            if self.terminals.is_none() {
                self.terminals =
                    Some(crate::terminals::NativeTerminals::new(vfs).map_err(io::Error::other)?);
            }
            let mut buffers = self
                .session
                .buffers
                .lock()
                .map_err(|_| io::Error::other("session lock failed"))?;
            self.terminals
                .as_mut()
                .expect("initialized")
                .pump(&mut buffers);
        }
        if let Ok(command) = self.commands.try_recv() {
            let (result, response) = match command {
                Command::Transfer(command) => (
                    self.transfer(vfs, command.options, &command.cancelled),
                    command.result,
                ),
                Command::Timezone(timezone, response) => (
                    trames_distro::synchronize_timezone(vfs, &timezone.identifier, &timezone.data)
                        .map_err(|e| e.to_string()),
                    response,
                ),
            };
            self.snapshot(vfs);
            let _ = response.send(result);
        }
        if self.snapshot_at.elapsed() >= Duration::from_secs(1) {
            self.snapshot(vfs);
            self.snapshot_at = Instant::now();
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
        let Some(b) = b.terminals.get_mut(&1) else {
            return Ok(bytes.len());
        };
        let count = bytes.len().min(QUEUE_LIMIT - b.output.len());
        if count == 0 && !bytes.is_empty() {
            return Err(io::ErrorKind::WouldBlock.into());
        }
        b.output.extend(&bytes[..count]);
        Ok(count)
    }
    fn poll_stdin(&mut self, bytes: &mut [u8]) -> io::Result<usize> {
        if self.session.multiple_terminals {
            return Err(io::ErrorKind::WouldBlock.into());
        }
        let mut b = self
            .session
            .buffers
            .lock()
            .map_err(|_| io::Error::other("session lock failed"))?;
        let b = b
            .terminals
            .get_mut(&1)
            .ok_or_else(|| io::Error::other("primary terminal closed"))?;
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
        if self.session.multiple_terminals {
            return Ok(false);
        }
        Ok(self
            .session
            .buffers
            .lock()
            .map_err(|_| io::Error::other("session lock failed"))?
            .terminals
            .get(&1)
            .is_some_and(|b| !b.input.is_empty()))
    }
    fn terminal_event(&mut self) -> Option<TerminalEvent> {
        if self.session.multiple_terminals {
            return None;
        }
        self.session
            .buffers
            .lock()
            .ok()?
            .terminals
            .get_mut(&1)?
            .size
            .take()
            .map(|(rows, columns)| TerminalEvent::Resize { rows, columns })
    }
}

#[cfg(test)]
mod memory_tests {
    use super::*;
    #[test]
    fn runtime_tmpfs_is_fresh_after_cold_disk_reopen() {
        let path = std::env::temp_dir().join(format!(
            "trames-runtime-mounts-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        {
            let mut vfs = Vfs::open_disk(&path, DiskOptions::default()).unwrap();
            prepare_runtime_mounts(&mut vfs).unwrap();
            for file in [b"/run/stale.pid".as_slice(), b"/dev/shm/stale".as_slice()] {
                let fd = vfs
                    .open_at(trames_vfs::AT_FDCWD, file, 0x41, 0o600)
                    .unwrap();
                vfs.write(fd, b"old session").unwrap();
                vfs.close(fd).unwrap();
            }
            vfs.sync().unwrap();
        }
        {
            let mut vfs = Vfs::open_disk(&path, DiskOptions::default()).unwrap();
            prepare_runtime_mounts(&mut vfs).unwrap();
            assert!(vfs
                .stat_at(trames_vfs::AT_FDCWD, b"/run/stale.pid")
                .is_err());
            assert!(vfs
                .stat_at(trames_vfs::AT_FDCWD, b"/dev/shm/stale")
                .is_err());
            assert_eq!(
                &vfs.statfs_at(b"/dev/shm").unwrap()[..8],
                &0x01021994u64.to_le_bytes()
            );
        }
        std::fs::remove_dir_all(path).unwrap();
    }
    #[test]
    fn runtime_tmpfs_precedes_wayland_and_repeated_preparation_preserves_live_files() {
        let mut vfs = Vfs::default();
        prepare_runtime_mounts(&mut vfs).unwrap();
        for (path, mode) in [
            (b"/run".as_slice(), 0o755),
            (b"/dev/shm".as_slice(), 0o1777),
        ] {
            assert_eq!(
                &vfs.statfs_at(path).unwrap()[..8],
                &0x01021994u64.to_le_bytes()
            );
            assert_eq!(
                u32::from_le_bytes(
                    vfs.stat_at(trames_vfs::AT_FDCWD, path).unwrap()[16..20]
                        .try_into()
                        .unwrap()
                ) & 0o7777,
                mode
            );
        }
        let shm = vfs.statfs_at(b"/dev/shm").unwrap();
        let block = u64::from_le_bytes(shm[8..16].try_into().unwrap());
        let blocks = u64::from_le_bytes(shm[16..24].try_into().unwrap());
        assert_eq!(block * blocks, 64 << 20);
        assert_eq!(
            &shm[16..24],
            &shm[24..32],
            "empty shm is lazy, not preallocated"
        );
        let run = vfs
            .mount_id_at(trames_vfs::AT_FDCWD, b"/run", false)
            .unwrap();
        let shm = vfs
            .mount_id_at(trames_vfs::AT_FDCWD, b"/dev/shm", false)
            .unwrap();
        let _wayland = vfs.enable_wayland(b"/run/user/0").unwrap();
        let socket = vfs
            .stat_at(trames_vfs::AT_FDCWD, b"/run/user/0/wayland-0")
            .unwrap();
        let fd = vfs
            .open_at(trames_vfs::AT_FDCWD, b"/run/live.pid", 0x41, 0o600)
            .unwrap();
        vfs.write(fd, b"123\n").unwrap();
        vfs.close(fd).unwrap();
        prepare_runtime_mounts(&mut vfs).unwrap();
        assert_eq!(
            vfs.mount_id_at(trames_vfs::AT_FDCWD, b"/run", false)
                .unwrap(),
            run
        );
        assert_eq!(
            vfs.mount_id_at(trames_vfs::AT_FDCWD, b"/dev/shm", false)
                .unwrap(),
            shm
        );
        assert_eq!(
            vfs.stat_at(trames_vfs::AT_FDCWD, b"/run/user/0/wayland-0")
                .unwrap()[8..16],
            socket[8..16]
        );
        assert_eq!(
            u64::from_le_bytes(
                vfs.stat_at(trames_vfs::AT_FDCWD, b"/run/live.pid").unwrap()[48..56]
                    .try_into()
                    .unwrap()
            ),
            4
        );
        let peer = vfs.clone();
        assert_eq!(
            peer.mount_id_at(trames_vfs::AT_FDCWD, b"/run", false)
                .unwrap(),
            run
        );
    }
    #[test]
    fn managed_dns_is_routable_for_containers_and_preserves_custom_configs() {
        let mut vfs = Vfs::default();
        for dir in [b"/etc".as_slice(), b"/root".as_slice()] {
            if vfs.stat_at(trames_vfs::AT_FDCWD, dir).is_err() {
                vfs.mkdir_at(trames_vfs::AT_FDCWD, dir, 0o755).unwrap();
            }
        }
        let read = |vfs: &mut Vfs| {
            let fd = vfs
                .open_at(trames_vfs::AT_FDCWD, b"/etc/resolv.conf", 0, 0)
                .unwrap();
            let bytes = vfs.read(fd, 1024).unwrap();
            vfs.close(fd).unwrap();
            bytes
        };
        prepare_guest_defaults(&mut vfs, "full").unwrap();
        assert_eq!(read(&mut vfs), b"nameserver 169.254.0.53\n");
        prepare_guest_defaults(&mut vfs, "loopback").unwrap();
        assert_eq!(read(&mut vfs), b"nameserver 127.0.0.53\n");
        prepare_guest_defaults(&mut vfs, "full").unwrap();
        assert_eq!(read(&mut vfs), b"nameserver 169.254.0.53\n");
        let fd = vfs
            .open_at(trames_vfs::AT_FDCWD, b"/etc/resolv.conf", 1 | 0x200, 0)
            .unwrap();
        vfs.write(fd, b"nameserver 1.1.1.1\n# user override\n")
            .unwrap();
        vfs.close(fd).unwrap();
        prepare_guest_defaults(&mut vfs, "full").unwrap();
        assert_eq!(read(&mut vfs), b"nameserver 1.1.1.1\n# user override\n");
    }
    #[test]
    fn optional_json_budgets_preserve_defaults_and_validate_before_start() {
        let base = serde_json::json!({"store":"/tmp/trames-memory-options", "volume_total": 1 << 30, "volume_available": 1 << 30});
        let options: Options = serde_json::from_value(base.clone()).unwrap();
        assert_eq!(
            memory_limits(&options).unwrap(),
            trames_core::MemoryLimits::default()
        );
        let mut custom = base;
        custom["memory_budget_bytes"] = serde_json::json!(4usize << 30);
        custom["process_memory_budget_bytes"] = serde_json::json!(2usize << 30);
        let options: Options = serde_json::from_value(custom.clone()).unwrap();
        assert_eq!(memory_limits(&options).unwrap().per_process, 2 << 30);
        custom["process_memory_budget_bytes"] = serde_json::json!(8usize << 30);
        let options: Options = serde_json::from_value(custom).unwrap();
        assert!(Session::start(options).is_err());
    }
}
