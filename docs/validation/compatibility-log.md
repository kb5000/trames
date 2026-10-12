# File ABI and runtime log — source follow-up to 0.8

Validated on 2026-10-12. This source update follows 0.8.0/build 9 and is intended
for the next combined app update. The existing IPA does not contain it. No new
IPA is produced by this batch; the embedding ABI and disk format remain unchanged.

## Selected interfaces

These interfaces have clear bounds and reuse the existing file/descriptor model,
so they improve ordinary applications before larger changes to shared mappings,
Unix ancillary messages or SVE architectural state.

| Interface | Implemented scope and limits |
| --- | --- |
| statx (291) | Real basic metadata, timestamps, device numbers and virtual mount ID; symlink/no-follow and empty path, including fd/current directory. No invented birth time, direct-I/O alignment or newer optional fields. FORCE_SYNC/DONT_SYNC are accepted within the existing synchronous virtual metadata model |
| preadv/pwritev (69/70) | Up to 1024 iovecs and 1 MiB transferred, complete buffer preflight before I/O, full native ARM64 offset, partial/EOF results and unchanged open-description position; existing Linux O_APPEND behavior applies |
| close_range (436) | Inclusive fd range, CLOEXEC and UNSHARE; the scheduler separates CLONE_FILES views before changing the caller's descriptor table. Work is bounded by existing guest fds, including when last is UINT_MAX |
| syslog/klogctl (116) | CLOSE/OPEN, READ, READ_ALL, READ_CLEAR, CLEAR, SIZE_UNREAD, SIZE_BUFFER. Effective guest UID 0 required. Console-control actions are unsupported; this never accesses the host kernel log |

That batch had 138 implemented subsets in the Linux v6.12 AArch64 inventory, 178
missing entries and 2 obsolete entries. All 180 undispatched numbers returned
ENOSYS on the actual ARM64 release build. Entry presence is not full ABI conformance.
The subsequent [shared mapping/ABI batch](mmap-coverage.md) updates those totals.
See the regenerated [CSV](compatibility-current-syscalls.csv) and [source fingerprints](compatibility-current-syscalls.json).

## Guest log behavior

`/dev/kmsg` is a volatile character device, major 1/minor 11, initially root-only
mode 0600. Each open has an independent sequence cursor; dup/fork aliases share
that cursor. Reads return Linux-style `priority,sequence,microseconds,-;message`
records. Short buffers return EINVAL without consuming a record, an empty
nonblocking reader returns EAGAIN, and an overwritten cursor returns EPIPE then
moves to the oldest retained record. Poll/epoll readiness and blocking reads are
connected to the cooperative scheduler. Zero-offset SEEK_SET/END/DATA select the
oldest/end/last-clear sequence; positioned I/O is rejected with ESPIPE.

The log is shared by virtual processes and survives exec and child exit. It is
scoped to the runtime session, is not persisted, and consumes no guest disk quota.
Both text representations together occupy at most 256 KiB; a message is capped
at 1024 bytes. At most 64 messages per second are retained, with a suppressed
message count emitted on the next append after the window expires. Control
characters are escaped. Root may write bounded user messages to `/dev/kmsg`;
this is not a full syslog daemon or `/dev/log` Unix socket service.

ENOSYS/EOPNOTSUPP syscall results record PID, the pinned syscall name/number,
SVC PC, all six raw guest arguments and errno. CPU faults record PID/TGID, PC,
precise instruction encoding for SIGILL/traps, or the memory-fault information.
Unsupported and architecturally unallocated encodings share the SIGILL
classification; no speculative disassembly is presented. These diagnostics do
not change the return value, guest signal handling or existing root-fault stderr.
A feature-probing program may deliberately trigger ENOSYS and then use a fallback.

```sh
dmesg
dmesg | tail -n 30
dmesg -c                 # read then clear the snapshot view
echo "guest diagnostic" > /dev/kmsg
```

Clearing the snapshot preserves independent `/dev/kmsg` readers and the legacy
READ stream, matching the selected Linux interfaces. `cat /dev/kmsg` follows
future records and can remain blocked; use `dmesg` for a snapshot. Follow modes
in other dmesg implementations still depend on their own flags/features.

## Validation

- Workspace: 178 tests passed, including descriptor unsharing/CLOEXEC, buffer
  preflight, real metadata fields, fork-shared fault records, independent readers,
  clear semantics, permissions, ring overrun, partial READ progress and rate limit.
- Actual ARM64 release interpreter: the same static musl file fixture as QEMU
  10.0.13, on memory and persistent disk stores. Includes offsets above 4 GiB,
  EOF, symlinks, invalid masks and flags, unchanged offsets and fd closure.
- CLONE_FILES-only clone with and without CLOSE_RANGE_UNSHARE: independently
  checked against the native Linux kernel. QEMU user rejects that Linux-valid
  clone form with EINVAL, so it is not used as the oracle for this case.
- Real child ENOSYS (named and unknown calls) and PTRUE/SVE SIGILL records;
  READ_CLEAR, independent cursors, blocking follow reader and actual BusyBox
  dmesg after exec, in both storage modes.
- M2: 24 QEMU comparisons in each storage mode (22 shell cases, process ABI and musl libm), using the actual ARM64 interpreter.
- Existing BusyBox: 18 QEMU comparisons and host-path isolation; existing actual
  musl fault handlers: seven scenarios including context editing/recovery.
- fmt/clippy with warnings denied and iOS target compilation passed. Native iPad
  execution of this source follow-up remains for the next packaged update.

Reproduce after the toolchain and static BusyBox prerequisites:

```sh
cargo build -p trames-cli --locked
bash scripts/build-busybox.sh
bash scripts/build-compatibility-log-fixtures.sh
python3 tests/integration/compatibility-log.py
python3 scripts/audit-syscalls.py
```

CI builds the fixture and runs it with the actual ARM64 interpreter. The host
fixture is only an independent Linux syscall oracle; it is not a guest backend.
