# Shared mappings and broader Linux ABI — source follow-up

Validated 2026-10-12. This follows the bounded-log/file-ABI batch after
0.8.0/build 9. It adds 23 numbered syscall entries and expands mmap, mprotect
and fcntl semantics. The current Linux v6.12 AArch64 inventory has **161
implemented subsets, 155 missing entries and 2 obsolete entries**. All 157
undispatched numbers were probed with the actual ARM64 cached interpreter and
returned ENOSYS. Entry presence is not complete ABI conformance.

This source is intended for the next combined app update. No new IPA is produced
here, and the existing build 9 does not contain it. The disk format and embedding
ABI remain unchanged. [Current gaps](../roadmap/compatibility-current.md),
[syscall CSV](compatibility-current-syscalls.csv) and
[source fingerprints](compatibility-current-syscalls.json) are regenerated.

## Implemented scope

| Interface | Behavior |
| --- | --- |
| mmap / mprotect | Coherent shared file and anonymous pages; private clean file pages and per-page COW; shared read-only descriptors cannot gain write access through mprotect |
| File EOF and truncation | Zero-filled last-page tail without extending the inode, shared tail bytes while mapped; full pages past EOF fault with SIGBUS/BUS_ADRERR and the accessed address; truncation/regrowth refreshes shared pages |
| msync (227) | Validate flags/alignment/range, flush shared file backing, propagate I/O errors; ASYNC can complete synchronously. Sync is deduplicated per inode, rather than fsync on every mapped page |
| mremap (216) | Shrink, in-place growth, MAYMOVE and non-overlapping FIXED moves; retain bytes, protections, COW/shared backing and zero-filled new anonymous pages |
| mincore (232) | Residency of valid, eagerly allocated runtime page objects; mapped full pages past file EOF report zero; missing mappings and invalid vectors fail |
| madvise (233) | NORMAL/RANDOM/SEQUENTIAL/WILLNEED, huge-page/dump advice and real DONTNEED discard of anonymous/private pages; clean private file pages reload the coherent file view |
| memfd_create (279) / fcntl | Anonymous RAM inode, CLOEXEC and ALLOW_SEALING; classic SEAL/SHRINK/GROW/WRITE seals, EBUSY with writable shared mappings, shared data and lifetime independent of fd closure |
| sendfile (71) / copy_file_range (285) | Bounded copy, current or supplied offsets, offset-pointer preflight, partial output, overlap/append checks; these perform copies rather than host zero-copy I/O |
| preadv2/pwritev2 (286/287) | Native ARM64 offset including -1 for current position, selected DSYNC/SYNC flags and bounded vectors; preserve existing offset and fault preflight rules |
| openat2 (437) | Versioned open_how size/tail/flags/mode checks and resolve=0; every nonzero containment requirement is explicitly rejected |
| fchmodat2 (452) | Path/fd/empty-path and no-follow mode changes; symlink mode changes are unsupported |
| readahead (213) / fadvise64 (223) | Validate descriptors, ranges and advice. Prefetch/eviction advice remains optional, rather than promising a host cache operation |
| syncfs (267) | Sync the virtual filesystem for a valid guest descriptor |
| sched_* (118–123, 125–126) / getcpu (168) | The single virtual CPU supports SCHED_OTHER with priority zero and CPU-0 affinity. Validate target task/owner, output masks and parameters; policy priority bounds are queryable |

The existing mapped-memory budgets remain 512 MiB per process and 1 GiB across
address spaces. memfd contents have a separate **128 MiB aggregate RAM budget**;
they do not enter the disk manifest or consume the root volume's logical quota.
Mappings retain inode/page objects after close/unlink and share them across fork.
They do not share arbitrary host memory or map Files-provider resources live.

## Coherence and decoder behavior

Each mapped file page has one coherent buffer per inode/offset, referenced by
mapping objects. Ordinary fd reads observe dirty mapped bytes; ordinary writes
update live mapped buffers. Only weak page references remain in the inode cache,
and the last mapping releases its cache entry. Disk imports use bounded 1 MiB
reads rather than opening the private object separately for every page.

Private writes take an Arc COW copy; fork shares only the intended shared pages.
Truncation, write protection and whole cross-page accesses are checked before
stores. Shared code writes through another mapping or a file descriptor update
backing epochs and page tokens, so the cached interpreter cannot keep executing
an obsolete instruction. No new CPU arithmetic instruction or backend is added.

## Remaining limitations

This is a bounded virtual memory model, with eager page objects rather than a
Linux demand-paging/reclaim subsystem. Huge pages, host pinning/eviction, MAP_SYNC
and device mappings are not provided. mremap does not support zero-size shared
alias duplication, DONTUNMAP or ranges crossing different mapping/protection
segments. madvise does not support DONTFORK/FREE/REMOVE or advanced reclaim.

memfd does not provide huge-page/no-exec modes or newer FUTURE_WRITE/EXEC seals.
openat2 containment flags, additional RWF flags, realtime scheduling and multiple
virtual CPUs remain unsupported. The older selected flags, quotas and Linux ABI
limits still apply. `/dev/kmsg` and dmesg continue to record unsupported calls and
CPU faults; a supported entry may still reject an unsupported option.

## Evidence

- 185 workspace tests passed, including shared anonymous fork behavior, protection
  isolation, atomic remap rejection, zero-filled growth, fd/map alias coherence,
  private discard, truncation, read-only maximum rights, decoder tokens, unlinked
  inode lifetime, cache release and copy/open_how failure preflight.
- Real static musl fixture runs on the actual ARM64 release interpreter, in memory
  and disk modes, compared with QEMU 10.0.13. Covers shared/private/anonymous mmap,
  fork, EOF/truncation SIGBUS, precise BUS_ADRERR handler/address, msync/remap,
  residency/discard, memfd/seals, executable aliases, file copies and metadata APIs.
  Shared-write persistence is also checked by closing and reopening the disk store.
- Scheduler-only cases use the native Linux kernel as a separate oracle. musl
  1.2.5 intentionally stubs some public sched wrappers with ENOSYS, so the fixture
  calls those kernel syscalls directly. This does not change that libc behavior.
- Real Alpine Python: mmap/fd coherence, fork, flush, resize and SQLite with mmap
  enabled passed on the actual ARM64 interpreter. Local verification reuses a copy
  of an earlier installed signed-package store; installation is not retested here.
  The workflow is also added to M6's fresh signed-package CI test for future runs.
- Existing diagnostics and seven musl fault-recovery scenarios passed; M2 passed
  24 QEMU comparisons in each storage mode. CPU/memory regressions passed 67 M1
  comparisons, 6,573 vector-copy/shift snapshots and 615 structure variants.
- fmt, clippy with warnings denied, ARM64 release build and iOS target compilation
  passed. Physical iPad testing awaits the next packaged update.

Reproduction after the toolchain/static BusyBox prerequisites:

```sh
bash scripts/build-mmap-fixtures.sh
cargo build -p trames-cli --target aarch64-unknown-linux-musl --release --locked
TRAMES_RUNNER=tests/differential/arm-runner.sh python3 tests/integration/mmap.py
python3 scripts/audit-syscalls.py
```

The host fixture is only an independent Linux syscall oracle. The guest remains
AArch64, and the shipping iPad interpreter remains ARM64 without JIT.
