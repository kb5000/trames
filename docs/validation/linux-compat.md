# Linux compatibility validation, 0.2.0

The owner reported these defects from installed 0.1.0 on a physical iPad. The
subsequent software checks run unmodified AArch64 Alpine 3.22.6 executables;
new-release physical acceptance remains pending. No command-output fixtures or
shell wrappers replace the guest tools.

Baseline `apk add vim` reproduced exactly 88 directory permission update errors:
fchownat returned ENOSYS and inode owner metadata was fixed at 1000. The fix stores
and enforces virtual ownership; installing the upstream signed Vim package set
into a fresh disk store finishes successfully without these errors. Older disk
manifests remain readable; tests cover metadata persistence and legacy migration.

Vim's timeout initialization uses musl SIGEV_THREAD. Its pthread clone flags were
previously rejected, so libc returned EAGAIN. Actual shared-VM clone, futex waits,
thread IDs and POSIX timers now support this path. An interactive test starts
ordinary `/usr/bin/vim` with Alpine's default vimrc and normal swap behavior,
answers terminal probes, edits, writes and quits without E1286. A separate runtime
reopens the disk store and reads the exact saved bytes.

`tests/integration/linux-compat.py` exercises 20 checks: four musl pthread/timer
comparisons against QEMU on memory/disk, real ps/mount/df/free/top/ip queries, two-
family loopback/gateway ping, runtime devices/statistics, two native ICMP egress
outcomes, signed fresh Vim installation and durable interactive editing. Local
native ICMP group permissions deny creation, and the test checks the actual
permission error. Independent isolated Docker tests enable ICMP datagram access
only in the container's own network namespace: IPv4 and IPv6 requests mapped to
its real loopback each receive 2/2 replies. Linux CI requires positive egress and
sets ping_group_range on its disposable runner. None of these tests changes host
routes, exposes a host raw socket, or establishes public Internet/iPad reachability.

CPU additions have independent QEMU comparisons: vector floating covers 3,980
cases across 166 executions; pairwise/scalar integer covers 356 register snapshots.
The Rust workspace now has 115 passing unit/integration tests. Unit checks
exercise DAC/sticky/SGID/set-ID behavior, volatile pseudo files,
FD/inode/mount identity, malformed IO preflight, shared address spaces, signal
targets, actual futex queues and timer deadlines. Netlink tests reject malformed multipart sends without
partial replies and check request/reply quotas, PEEK, sequence and DONE messages.
Both manifest v1 and v2 preserve historical owner/data while upgrading to v3.
Existing differential and
integration suites remain required in Linux CI, together with the iOS target
compile check and Apple simulator/build workflow.

Reproduce after the standard toolchain and pinned rootfs preparation:

```sh
bash scripts/build-linux-compat-fixtures.sh
cargo build --release --workspace --locked
python3 tests/integration/linux-compat.py
```

For positive native egress on a disposable Linux test runner, allow the runner's
group to use ICMP datagram sockets via ping_group_range and set
`TRAMES_REQUIRE_ICMP_EGRESS=1`. Do not change a shared host's policy merely to hide
a permission failure. The rootfs digest/pinned musl build remain those in the M3
and BusyBox records; apk verifies live upstream index/package signatures against
the pinned rootfs trust keys. Upstream Vim package versions can advance.

On iPad after an overwrite update, check `ps`, `mount`, `df -h`, `free`, `top -b
-n 1`, `ip a`, `ip route`, `ping -c 2 127.0.0.1`, and a permitted reachable
Internet peer. Install Vim and edit/save/reopen a file; scroll, rotate and inspect
apk progress for the prior horizontal viewport bug. Unsupported arbitrary
mounts, multiple PTY allocation, kernel modules, ACLs/xattrs, AF_UNIX and full
Linux networking are documented in the [architecture](../architecture/linux-compat.md).
