# systemd integration

Status: first runtime blockers repaired, 2026-10-31. Original Debian systemd
reaches `basic.target` in a disposable Mac HVF environment, with failed units.
This is degraded-boot evidence, not complete systemd or product boot support.
Shared runtime: [`abec8df`](https://github.com/kb5000/trames-runtime/commit/abec8df785ba04223f65e5311aee8c82bd256914).

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
working environments and their configuration were not modified. The probe uses an independently built, signed Mac runner and the same shared
runtime code tested through the CPU interpreter. The user's normal environments
are not converted to systemd boot mode.

The signed Debian repository supplied `systemd 257.13-1~deb13u1` and
`dbus 1.16.2-2`. Package bytes and systemd were not patched.

| Probe | Result and scope |
| --- | --- |
| `systemd --version` | Runs the original glibc executable and reports its compiled features; this is not boot acceptance. |
| `dpkg --configure -a; dpkg --audit` after package unpack | Both complete successfully after the OFD lock implementation. |
| OFD locking | F_OFD_GETLK=36, F_OFD_SETLK=37 and blocking F_OFD_SETLKW=38 now use the common range-lock engine. The original sysusers passwd lock succeeds. |
| Original systemd as guest PID 1 | RLIMIT_CORE initialization, native clock-change notification, generator sandbox mounts and manager notification sockets now succeed. |
| `--system --unit=basic.target`, 4 CPUs | Original systemd reports `basic.target` active and startup complete in 1.458 seconds. Some constituent units fail; this is not a clean boot. A prior 2-CPU run reported 1.915 seconds. These are observations, not benchmark medians. |

No package patches, unit masks, forced-success syscall responses, relaxed
service sandbox settings or Linux kernel VM are used. The guest-only
`tests/fixtures/systemd-boot.c` prepares private `/run`, `/dev`, `/dev/shm`,
devpts and a retained console, then execs the distribution init. It refuses to
run outside Trames PID 1. Build with `scripts/build-systemd-fixtures.sh`, import
outside `/tmp` (which the real boot may overmount), and run in a disposable
Debian environment with `--pty`. A host timeout ends the probe after observing
startup; it does not verify orderly systemd shutdown.

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

`tests/integration/systemd-abi.py` compares the same OFD/rlimit/timer and Unix
notification fixtures against native Linux on 1 and 4 guest CPUs. Both CPU
interpreter and Mac HVF probes pass, including forked stream writer identities,
MSG_PEEK, SCM_RIGHTS, close-on-exec and datagram truncation. Scoped VFS/Linux ABI
suites pass on macOS and Linux, the session conformance suite passes after
rerunning only network-permission failures with normal host access, and the
common iPad adapter still compiles. Raw logs and the disposable store remain
ignored under `target/systemd-fix` and `target/systemd-probe`.

### Remaining measured boot blockers

- `systemd-journald.service` fails at RUNTIME_DIRECTORY with EINVAL after its
  ID-mapping fallback. Journal sockets consequently hit the start limit and
  journal flushing fails. The failing syscall path still needs isolation.
- Credential directory setup for sysusers/tmpfiles/sysctl requires legacy
  MS_MOVE, currently EOPNOTSUPP. The real credential sandbox remains enabled.
- Mount helpers return success, but libmount's mount-change drain returns
  EINVAL and PID 1 does not observe the new mounts. `tmp.mount` and
  `run-lock.mount` therefore fail with result `protocol`. Mount-table event
  visibility needs implementation and a native-reference regression.
- Container API preparation must expose synthetic sysfs read-only before boot.
  The current fixture has not completed that contract: the modules-load unit
  still runs and fails in this userspace-only environment. Do not hide it with
  a success stub for kernel module loading.

These are the next compatibility targets before ordinary service, D-Bus,
journal and shutdown acceptance. No boot-mode selector or independent
systemd lifetime/attach API is installed in the Mac environment manager yet.

## Implementation sequence

### 1. First common ABI gaps (implemented above)

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
