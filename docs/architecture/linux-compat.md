# Linux userspace compatibility, 0.2.0

This release follows the owner's physical iPad reports: missing runtime files,
failed `ps`/`mount`/`df`/`ip`/`ping`, 88 apk directory ownership errors, and Vim
E1286. Files describe the virtual runtime, never the host's processes, descriptors,
addresses or files. Unmodified Alpine executables exercise the implementation.

## Runtime files

| Namespace | Implemented views |
| --- | --- |
| `/proc` | self/thread-self, numeric process and task directories, stat/status/statm, cmdline/environ/comm, maps/auxv, cwd/root/exe links, fd/fdinfo, limits, mounts/mountinfo |
| System `/proc` | uptime, meminfo, cpuinfo, version, stat, loadavg, filesystems, swaps, partitions, devices, diskstats, selected read-only sysctl values |
| Network `/proc` | dev, route, if_inet6, tcp/tcp6, udp/udp6, raw/raw6, icmp/icmp6; empty unix table while AF_UNIX is unsupported |
| `/sys` | class/net links to devices/virtual/net, logical interfaces and address/MTU/state/index/flags attributes, packet/byte/drop counters |
| `/dev` | null, zero, full, random/urandom, tty, pts/0, fd/stdin/stdout/stderr links, ptmx endpoint with unsupported allocation reported explicitly |

Files are generated from bounded scheduler/VFS/network snapshots; command names
are not intercepted. Process state, IDs, masks, memory maps and descriptor
positions reflect guest objects. Link counters reflect fabric packets rather than
host NICs. Socket tables deduplicate shared descriptions and show actual virtual
addresses/state/queued bytes; native backend queue sizes unavailable through the
safe adapter are zero. There are no block/swap devices. CPU/load/uptime describe
the cooperative interpreter, not host-wide telemetry.

Volatile inode IDs occupy a separate range, are excluded from persistent
manifests and file quotas, and have a 32,768-node cache limit. Dead process/task
entries are pruned. Read-only proc/sys mounts reject mutations. Sensitive process
files and descriptor directories enforce guest owner permissions. Proc FD magic
links retain actual open inode identity, including deleted files, without exposing
a host descriptor. Mount IDs/device numbers, filesystem magic/flags and directory
link counts agree with the virtual namespace. Root disk capacity uses the stored
logical budget and platform volume snapshot; pseudo mounts report no disk blocks.
`mount` lists built-in mounts; arbitrary mount/umount operations are unsupported.
There is one controlling PTY, not an allocator for multiple PTY pairs.

## Ownership and persistence

Guest sessions default to root for ordinary Alpine administration. This changes
no host UID or app entitlement. Real/effective/saved/fs UID/GID and supplementary
groups are modeled, with owner/group/other access, root execute checks, sticky
directories, SGID inheritance, set-ID executable credentials and privileged-mode
clearing on ownership changes/non-root writes. `chown`/`fchown`/`fchownat`, chmod,
access and identity syscall subsets operate on virtual inode metadata. Imported
tar ownership and 07777 mode bits are retained.

Manifest version 3 stores inode UID/GID; versions 1 and 2 remain readable and
their historical owner 1000 is preserved rather than silently rewritten. The next
checkpoint writes v3. Ownership, modes and symlinks survive reopening. No install
step resets an existing store; the app keeps `org.trames.Trames` for an overwrite
update. Linux capabilities, ACLs, xattrs and complete namespace semantics remain
outside this implementation.

## Threads, waits and timers

Musl pthread clone uses genuinely shared guest memory/maps, separate CPU/TLS,
TID/TGID, shared files/cwd/signal actions as requested by clone flags, and
clear_child_tid futex wake on exit. Fork still creates an independent COW address
space. vfork blocks its parent until child exec/exit. Nonleader exec and group
exit preserve thread-group identity/lifetime. Everything runs cooperatively;
guest threads create no host threads. Limits are 32 tasks, 64 MiB per distinct
address space and 256 MiB aggregate; shared memory is charged once.

Futex WAIT/WAKE, bitsets and selected requeue operations have actual wait queues,
value checks and monotonic/realtime deadlines. POSIX timers support realtime and
monotonic clocks, signal/none/thread-ID notification, relative/absolute deadlines,
intervals and overrun accounting. `rt_sigtimedwait` consumes matching signals with
Linux siginfo, allowing musl SIGEV_THREAD callbacks. ITIMER_REAL supplies SIGALRM.
Unsupported operations and CPU-time timers return errors. Robust futex recovery,
PI futex, alternate signal stacks and full realtime-signal queuing are not claimed.
Thread signals check TID/TGID and guest UID permissions; ordinary process-directed
signals are queued once per group. This is still a selected Linux signal ABI.

Readv validates every destination before consuming input; supported blocking
reads resume through the scheduler. sysinfo and getrlimit/prlimit report actual
budgets. Changes to unenforced resource limits are rejected. Scalar AdvSIMD FP
conversions and integer operations needed by real `top` and default Vim use the
same checked interpreter, verified independently against QEMU.

## Network queries and ICMP

AF_NETLINK/NETLINK_ROUTE is a guest-only read-only adapter supporting link,
address and route dumps for `ip a`/`ip route`, multipart replies, sequence numbers,
acknowledgments, bounded queues and explicit errors for mutations. It opens no
host netlink socket. Logical interfaces are lo and trames0 in virtual mode; native
mode exposes the supported loopback view rather than host interface metadata.

The virtual stack supports bounded SOCK_RAW ICMP/ICMPv6 plus existing ICMP
datagram echo. Raw means guest IP packets handled by smoltcp, never a host raw
socket. ICMP echo egress uses native unprivileged ICMP **datagram** sockets,
validates requests, binds replies to namespace/source/destination/identifier,
normalizes kernel-selected identifiers and rebuilds valid guest IP checksums.
At most 32 echo flows share the existing 64-flow gateway bound, with 60-second
idle expiry; payloads are 8–1400 bytes. Policy/OS permission errors propagate;
replies are never synthesized for an unavailable external peer. Non-echo external
raw ICMP and AF_PACKET are unsupported. Actual iPad ICMP permissions and routing
remain device acceptance work. See [virtual network](virtual-network.md).

Validation and reproduction: [linux-compat.md](../validation/linux-compat.md).
