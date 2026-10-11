# M6 Linux userspace compatibility

M6 keeps the AArch64 EL0 CPU profile from [0.4.0](compatibility-0.4.md).
This release extends the virtual Linux ABI for ordinary developer workflows.
It does not boot a Linux kernel. See [validation](../validation/m6.md) for
the tested binaries, inputs and delivery evidence.

| Interface | Implemented scope |
| --- | --- |
| epoll | Guest open descriptions, ADD/MOD/DEL, level and ONESHOT readiness, duplicate registration identity, last-close cleanup and fair delivery; epoll_pwait/pwait2 timeouts and temporary signal masks |
| eventfd | Shared 64-bit counters, semaphore, nonblocking and close-on-exec; overflow, fault preflight and cooperative waits |
| timerfd | Relative/absolute realtime, monotonic and virtual boottime timers, intervals, expiration counters, disarm and shared descriptor state |
| Unix IPC | AF_UNIX stream/datagram/seqpacket socket pairs, vectored messages, peek/truncation, shutdown, fork/dup and peer-close readiness |
| Files and synchronization | POSIX GETLK/SETLK/SETLKW record locks, TGID ownership, split/merge, closing aliases, blocking and deadlock detection; persistent FIFO nodes, blocking/nonblocking opens and generic FIONBIO |
| Credentials | exec retains the caller's UID/GID/groups and set-ID results; directory search permissions are enforced; adduser and root-to-user su are tested |
| Signal flags | SA_ONSTACK is accepted with the alternate stack disabled, so Linux's ordinary-stack behavior works for Python signal handlers; alternate-stack registration remains unsupported |
| Runtime endpoints | /proc/stat includes stable btime, cumulative virtual CPU ticks and context-switch count; htop is exercised through a real PTY |
| Path handling | Trailing-slash mkdir, missing-path errors and O_CREAT handling match tested Linux cases, allowing Git to create its template directories |

Queues remain bounded: 64 KiB per pipe/local socket and 128 queued local
datagrams. The guest file table has 256 entries. An epoll object can register
1024 fd/open-description identities; event output is capped at 1 MiB. These
are virtual runtime quotas, independent of host descriptors.

The mapped-memory quota is now 512 MiB per virtual process and 1 GiB across
distinct address spaces. This accommodates LLVM's shared library (over 130 MiB)
and compiler subprocesses. The main ELF import limit remains 64 MiB. Fork uses
COW memory; these logical limits do not guarantee that every iPad has enough
physical memory for every build.

The Linux v6.12 inventory contains 318 numbered entries: 133 implemented
subsets, 183 missing entries and 2 obsolete entries. All 185 undispatched
numbers return ENOSYS in both backends. Entry presence is not proof of every
flag, argument, concurrency pattern or Linux conformance.
[CSV](../validation/m6-syscalls.csv) and
[metadata](../validation/m6-syscalls.json) record the source fingerprints.

Remaining work includes named AF_UNIX endpoints and SCM_RIGHTS, EPOLLET,
nested epoll and exclusive watches, timer clock-change cancellation, OFD
locks, broader shared-memory/robust-futex behavior and synchronous CPU fault
recovery into guest signal handlers. Unsupported operations return an error.
Record-lock ownership is tested for fork and ordinary pthreads sharing a file
table; unusual clone combinations that share tables across TGIDs or give
threads private tables are not fully modeled.
Non-root su back to root requires Alpine's normal setuid applet setup; the
base minirootfs BusyBox is not a setuid authentication installation.

The reference backend stays the default. Debian, arbitrary packages and
physical iPad compiler workloads remain subsequent acceptance work.
