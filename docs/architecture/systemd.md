# systemd integration

Status: original-package boot, service, journal and halt acceptance passed,
2026-10-31, on Mac HVF with 1 and 4 guest CPUs. No units failed in these runs.
The Mac environment manager still starts shell environments by default; a
product systemd boot/attach mode is not enabled yet.
Shared runtime: [`1fafdad`](https://github.com/kb5000/trames-runtime/commit/1fafdad3ecb15f65527cb38b905d49c7be23bed7).

## Product and runtime boundary

Run the distribution's original ARM64 systemd as guest PID 1 through the shared
Linux userspace kernel. Mac continues to use thin HVF instruction/MMU execution;
there is no Linux kernel image, host init replacement or native `systemctl`
shim. Common Linux syscall/process/filesystem changes belong in trames-runtime
and retain interpreter/iPad compatibility. Mac owns environment configuration,
background lifetime and terminal attachment.

Use systemd's [container interface](https://systemd.io/CONTAINER_INTERFACE/):
identify the instance with `container=trames`, supply API mounts and a console,
keep synthetic sysfs read-only, and provide a consistent cgroup view. Instance
identity must persist across boots and differ between concurrently cloned
instances. Boot readiness and orderly shutdown should use systemd's documented
notification/signal interfaces. These are guest operations; no host sysfs,
cgroup tree, device node or reboot operation is forwarded.

## Original-package acceptance

A disposable copy of the already validated Debian 13.7 ARM64 store was used;
working environments and their configuration were not modified. The probe uses
an independently built, signed Mac runner and the same shared runtime code
tested through the CPU interpreter. The user's normal environments
are not converted to systemd boot mode.

The signed Debian repository supplied `systemd 257.13-1~deb13u1` and
`dbus 1.16.2-2`. Package bytes and systemd were not patched.

| Probe | Result and scope |
| --- | --- |
| `systemd --version` | Runs the original glibc executable and reports its compiled features; this is not boot acceptance. |
| `dpkg --configure -a; dpkg --audit` after package unpack | Both complete successfully after the OFD lock implementation. |
| OFD locking | F_OFD_GETLK=36, F_OFD_SETLK=37 and blocking F_OFD_SETLKW=38 now use the common range-lock engine. The original sysusers passwd lock succeeds. |
| Original systemd as guest PID 1 | Generator and credential sandboxes, journald and mount units start with their original configuration. Kernel-module units correctly skip because the boot fixture drops CAP_SYS_MODULE from the guest bounding set. |
| `--system --unit=basic.target` | Startup complete in 2.279 seconds with 1 CPU and 1.666 seconds with 4 CPUs, with no failed units. These are individual observations, not benchmark medians. |
| Original `systemctl` | Starts a oneshot unit, observes it active, starts a notify unit only after its real `READY=1`, and stops that unit successfully. |
| Original `journalctl` | Reads the oneshot's `TRAMES_SYSTEMD_SERVICE_OK` message from the guest journal. |
| `SIGRTMIN+3` shutdown request | The real init stops services, flushes the journal, detaches filesystems and executes its final halt. The runtime exits 0 without a host timeout. |

No package patches, unit masks, forced-success syscall responses, relaxed
service sandbox settings or Linux kernel VM are used. The guest-only
`tests/fixtures/systemd-boot.c` prepares private `/run`, `/dev`, `/dev/shm`,
devpts, read-only sysfs and a retained console, then execs the distribution
init. Its boot path refuses to run outside Trames PID 1. The fixture drops
CAP_SYS_MODULE before exec because this userspace container has no kernel module
loader; it does not replace module syscalls with success responses. Build with `scripts/build-systemd-fixtures.sh`, import
outside `/tmp` (which the real boot may overmount), and run in a disposable
Debian environment with `--pty`. Pass `--exercise` to the imported fixture for the full acceptance path. It creates
two additional test units under the private `/run`, drives the original manager
with `systemctl`, checks journal contents, and requests guest shutdown. A host
timeout is only a failure bound; successful runs finish by the guest halt.

### Shared runtime changes and validation

- OFD ownership follows the actual open file description across dup/fork and
  retained blocking calls. Closing/reusing the numeric FD cannot retarget an
  accepted wait. OFD/POSIX conflicts, split ranges and final release share the
  existing per-inode lock and scheduler resources.
- RLIMIT_CORE has process-owned soft/hard state, fork/thread inheritance,
  capability checks and a consistent `/proc/PID/limits` row. It does not change
  host limits or implement core-file output.
- Session `boot_id` is stable across tasks and new in an independent VFS.
- Absolute realtime timers support TFD_TIMER_CANCEL_ON_SET through native
  discontinuous-clock notifications: Apple libnotify or Linux timerfd. Existing
  executor FD waits handle wakeup, and reads report ECANCELED. Ordinary timers
  do not register an extra native watcher. The Apple regression posts only a
  unique private test notification; it never changes the system clock.
- Legacy MS_MOVE reattaches a complete mount subtree without replacing its
  mount/inode identity or existing open files. Cycles fail before changing the
  mount table. Mountinfo/mounts FDs pin their original namespace/root, expose
  current table contents, and notify through the existing executor subscriptions.
  Nested epoll supports libmount's event loop, with cycle/depth checks at
  registration, original event data and edge delivery, and weak OFD lifetime.
  IN_ISDIR is accepted as a non-selecting inotify request annotation.
- O_NOATIME has guest owner/capability checks and suppresses private-file access
  timestamp updates on owner and worker paths. F_SETFL uses the live shared
  status; enabling the flag remains an authority-changing joined operation.
  Legacy fstatat accepts AT_NO_AUTOMOUNT; this runtime has no automount side effect.
- The native Linux personality can be queried/reselected. Unsupported domains
  remain EINVAL. This lets stock systemd install its personality seccomp filter.
- Mounts through `/proc/self/fd/N` refresh descriptor identities. tmpfs accepts
  percentage sizes, scaled inode counts and explicit unlimited ceilings without
  preallocating the mount's declared capacity.
- Addressed Unix datagrams support filesystem/abstract destinations, connected
  send, source addresses, descriptor rights and real per-message credentials.
  SO_PASSCRED and SCM_CREDENTIALS also work on paired/stream sockets. Worker
  byte sends carry the calling task identity; credential-bearing receives
  defer before effects to the existing owner ABI. Explicit credentials require
  the corresponding guest authority. No host PID, socket pathname or account
  is substituted. io_uring sends without a caller identity remain unsupported
  when their peer requires credentials; general Unix socket completion is not
  claimed by these probes.
- Unix SO_TIMESTAMP/SO_TIMESTAMPNS (old/new time ABI) records actual queue-arrival
  time and returns the corresponding ancillary data. Socket send/receive buffer
  settings change queue ceilings, and payload grows on demand. Timestamp work
  is absent when the option is disabled.
- Guest halt/poweroff validates CAP_SYS_BOOT and Linux magic values, then uses
  the existing root-group exit/cleanup. It never invokes a host reboot API.
  Restart, kexec, suspend and nested PID namespace reboot remain EOPNOTSUPP.
  Syscall tracing now records all six argument words for further diagnosis.

`tests/integration/systemd-abi.py` compares the same OFD/rlimit/timer, Unix
notification/timestamp, and mount-lifecycle fixtures against native Linux on 1
and 4 guest CPUs. The native mount fixture runs as root only after creating a
private mount namespace; it never starts host init or changes the caller's
mount table. Both CPU
interpreter and Mac HVF probes pass, including forked stream writer identities,
MSG_PEEK, SCM_RIGHTS, close-on-exec, datagram truncation, buffers beyond 64 KiB,
mount-table event delivery, moved identities, and open namespace pinning. Scoped VFS/Linux ABI
suites pass on macOS and Linux, the session conformance suite and targeted guest-poweroff tests pass, and the
common iPad adapter still compiles. Raw logs and the disposable store remain
ignored under `target/systemd-fix` and `target/systemd-probe`.

### Remaining integration boundaries

The measured journald, credential-directory and mount-notification blockers
above are repaired. Optional debug warnings remain for interfaces such as
cgroup xattrs/IDs and kernel keyrings; they did not fail units in these runs.
This evidence covers the shipped Debian base units and the two acceptance
services, not every unit sandbox setting or a full desktop.

A product boot-mode selector, persistent init lifetime, exec/terminal attachment,
restart routing and manager diagnostics remain. D-Bus system-bus activation,
third-party sandboxed services, timer/socket activation and broader failure
recovery still need workload acceptance. Common iPad compilation passes; no
new device boot acceptance is claimed.

## Implementation sequence

### 1. Common boot ABI (accepted above)

The locks, task limits, timers, mount events, Unix notification/journal transport
and guest halt paths are implemented in the shared runtime. Continue measuring
interfaces required by real workloads; do not infer complete Linux compatibility
from a successful target or advertise unenforced cgroup controllers.

### 2. Reusable init preparation (fixture accepted)

The boot fixture supplies private API mounts, console and guest capability policy.
Move this setup into a reusable init-capable environment path before enabling
product boot mode. Preserve resolver settings and authorized host-directory
mounts, persist per-instance machine identity, and report guest readiness through
the documented notification protocol. Independent and cloned environments need
separate identities and a new boot ID for each boot.

### 3. Separate environment lifetime from terminal lifetime

Current Mac `env start` runs a single top-level command and owns the store until
that command/session exits. That works for a login shell; it does not provide an
independent systemd instance plus attachable clients. Add an explicit boot mode
(`shell` or `systemd`, initially Debian only) and let the existing runtime process
own the running environment independently of the manager window or client TTY.

Add `env exec`/`env shell` that enter that same live guest process kernel, sharing
its VFS, PID namespace, credentials and cgroups. A second runtime reopening the
store is not an implementation of exec/attach. The existing iPad multi-terminal
path already allocates guest PTY resources, but its private BusyBox launcher loop
cannot simply replace systemd as PID 1. Factor the useful PTY transport into the
common layer and choose a guest-side execution/activation protocol that works
with the real init. Host control handling should use existing event/executor
resources, without a new polling bus or resource coordinator.

Boot completion must mean a guest readiness notification, not just that the host
called Runtime::run. Stop must first request guest init shutdown, let services
finish and filesystems sync, and only then release the environment. Guest reboot
restarts that environment; it never reboots macOS. Explicit forced cancellation
remains a separate last-resort operation.

### 4. Integrate management and accept real workloads

Expose boot mode, starting/running/stopping/failed state, terminal attachment and
service diagnostics in the native manager. Preserve the current Alpine shell
workflow. Installing systemd is an explicit setup operation, not an apt install
performed on every environment start.

Acceptance should progress from original systemd reaching `basic.target` to
ordinary services: oneshot exit/status, a long-running process and its children,
restart policy, timer activation, Unix/TCP socket activation, D-Bus control and
journal output. Test two environments concurrently, client detach/reconnect,
clean shutdown, restart and failed-service recovery. Add PrivateTmp/ProtectSystem
and other sandboxed unit cases as their actual mount/capability paths are
implemented. A service starting with its sandbox disabled is not sandbox
acceptance. Keep journals in the private filesystem while shared-directory
MAP_SHARED remains unsupported.

Resource/controller extensions, logind, per-user sessions and device-oriented
services follow these results. Full desktop boot, udev hardware enumeration,
block-device services and complete systemd compatibility are not initial
acceptance claims. The native GPU/video APIs remain independent of init choice.
