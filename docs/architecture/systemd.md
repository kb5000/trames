# systemd integration

Status: design and original-package probes, 2026-10-31. systemd does not boot
successfully yet. Production execution code is unchanged by this investigation.

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

## Confirmed original-package probes

A disposable copy of the already validated Debian 13.7 ARM64 store was used;
working environments and their configuration were not modified. The installed
Mac runner includes the terminal-output correction on product main `a10c098`;
its shared runtime is `63bcace86a7b3c54be9e6d5f97ee5a23269c5da9`.

The signed Debian repository supplied `systemd 257.13-1~deb13u1` and
`dbus 1.16.2-2`. Package bytes and systemd were not patched.

| Probe | Result and scope |
| --- | --- |
| `systemd --version` | Runs the original glibc executable and reports its compiled features; this is not boot acceptance. |
| `apt-get install --no-install-recommends systemd dbus` | Downloads/unpacks packages, but systemd configuration fails in systemd-sysusers; D-Bus remains partially configured because of the dependency failure. |
| `systemd-sysusers` with syscall tracing | `fcntl(fd, 38, flock)` returns EINVAL. Command 38 is F_OFD_SETLK, used for the passwd lock. Current common code only implements POSIX F_GETLK/F_SETLK/F_SETLKW. |
| Original systemd, guest PID 1, `container=trames`, `--system --unit=basic.target` | Exits 255: setting RLIMIT_CORE fails with EPERM. Tracing confirms prlimit64 resource 4. Current `Kernel::rlimit` fixes its hard/soft limit at zero and refuses an increase even with guest CAP_SYS_RESOURCE. |

The shell uses `exec env ... systemd`, so PID 1 is retained. No unit masks,
forced-success syscall responses, relaxed service sandbox settings or Linux VM
are used to claim progress. Execution does not yet reach a successful basic
boot, so subsequent missing interfaces must be measured rather than inferred
from these first failures. Raw logs and the disposable store stay in ignored
`target/systemd-probe` output.

## Implementation sequence

### 1. Complete the first common ABI gaps

Implement OFD record locks using the existing per-inode lock/wakeup resources.
Ownership belongs to the open file description, survives dup/fork and retained
operations, and ends at its final release. OFD and POSIX locks must conflict
correctly; F_OFD_GETLK reports the proper owner semantics. Blocking requests use
the existing resumable executor. Do not translate OFD commands into process
locks or introduce a new global lock/service thread. Private files are the first
acceptance target; cross-host shared-directory locking retains its separately
stated limits until the native path is implemented and tested.

Replace fixed resource-limit answers where required with task/process state,
correct capability checks and fork/exec inheritance. Record a permitted
RLIMIT_CORE value without changing the native process's limit or claiming an
implemented core-dump writer. Enforced limits must continue to describe actual
guest behavior; raising a limit must not silently bypass a product-selected
memory/resource policy. Re-run systemd-sysusers and `dpkg --configure -a`, then
continue the original PID 1 startup to its next real failure.

### 2. Prepare an init-capable environment

The shared VFS already supplies proc/sysfs, tmpfs, PTYs and cgroup v2 objects.
Move reusable boot preparation out of the iPad-specific session layer: mount
fresh `/run` and `/dev/shm` before services, retain the console, prepare persistent
machine identity, and expose a session boot ID. Validate the full mount table,
read-only synthetic sysfs and writable guest cgroup view against actual systemd
queries. Existing user resolver settings and authorized host-directory mounts
remain product configuration.

The existing cgroup implementation has domain membership, pids, freezing and
kill. Check cgroup events/inotify, subtree lifecycle, delegation and task cleanup
with real units; do not advertise CPU/memory/io controllers until their limits
are actually enforced. Likewise, confirm which modern pidfd/clone/mount APIs
this exact systemd version requires or can fall back from. Do not implement
unreached interfaces merely to complete a syscall checklist.

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
