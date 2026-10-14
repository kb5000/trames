# Original-source native mallocng validation

Development follow-up to the immutable 0.13.0/build 14 release, tracked in
[#18](https://github.com/kb5000/trames/issues/18). This batch implements the
[existing-group allocation and atomic-free profile](../architecture/native-allocator.md)
from unchanged pinned musl 1.2.5 inputs. Cold allocator/kernel operations and
complete calloc/realloc/alignment bodies remain original guest code.

The profile is experimental and defaults off. CLI `--accelerator pure
--native-allocator` or embedding JSON `pure_accelerator: true,
native_allocator: true` activates routing. Ordinary pure acceleration retains
original mallocng. The measured adapter overhead does not justify default
activation yet.

## Correctness and safety

- 263 host workspace tests and 208 actual ARM64 tests under QEMU passed.
- 64 tests passed under ThreadSanitizer with instrumented Rust core/alloc/std/test:
  41 memory tests and 23 accelerator tests, without race reports or suppressions.
- Generated native C passed ASan/UBSan, including malformed addresses, sizes,
  control words and a forced failed-CAS slow continuation. The harness uses no
  heap allocation; LeakSanitizer is disabled because this workspace uses ptrace.
- Strict workspace Clippy and the iOS target compilation check passed. That
  local check is not an SDK-linked application or physical-iPad test.
- Eight real allocator rows passed: acceleration off at 1/4 CPUs, default pure
  at 4 CPUs, explicit allocator routing at 1/2/4 CPUs, shared stacks and disk
  storage. Original and patched musl under the independent QEMU oracle matched.
- Fixtures cover size-class boundaries, zero/overflow/ENOMEM, errno, calloc
  zeroing, realloc preservation, 16-byte through 1 MiB alignment, usable-size
  results, cross-thread free, fork/COW and exec capability preservation.
- A deterministic transport test models peer allocations consuming available
  and newly activated freed slots. Failed CAS returns the original metadata and
  slot index, publishes header invalidation exactly once, and selects guest slow
  continuation rather than original-function replay. Parallel-view tests check
  coherent frees and write-permission preflight before metadata publication.
- Existing full-libc loader/exports/TLS/startup tests, 476 exact integer oracle
  rows, all 19 pure functions and parallel parsing/nested callbacks passed.
- Opt-in Node.js 22.23.2/npm 11.6.4 passed filesystem/JSON/Intl/crypto/zlib/timers,
  SharedArrayBuffer/Atomics, workers/child processes, DNS/HTTP/fetch/virtual egress,
  offline dependency installation and persistence reopening.

Original upstream files, per-file hashes and COPYRIGHT are retained. Guest
patches check those original inputs, and the paired full-musl source package
contains the original archive, allocator headers/stubs and required vendored
malloc/free inputs. Public symbols remain standard musl names.

## Measured limits

Three alternating samples on the shared x86_64 development host used identical
guest ELF/libc/stores for baseline `8e9e35a` and the native prototype. This was
after restoring unlock-before-framing and adding register result transport and
nonvolatile-field caching, before adding the default-off flag and the exec-probe
fixture. Exact benchmark binaries, fixture and source were preserved; hashes and
all raw samples are in [the evidence data](native-allocator-data.json).

| Workload | Original allocator median | Native prototype median | Original/native ratio |
| --- | --- | --- | --- |
| Four allocation threads, 1 guest CPU | 1.372 s | 1.672 s | 0.82× |
| Four allocation threads, 4 guest CPUs | 11.437 s | 15.683 s | 0.73× |
| Normal Vim startup, 1 guest CPU | 2.036 s | 2.264 s | 0.90× |
| Python imports/SQLite/hash, 1 guest CPU | 8.909 s | 9.300 s | 0.96× |

The allocation workload uses 96 rounds of 192 allocations per thread, varied
1–2048-byte sizes, cross-thread frees and sampled payload checks. Its original
global malloc lock already limits scaling. Native entry/return, checked scalar
accesses and repeated original fallback work add cost; compiled arithmetic alone
does not remove those boundaries. Earlier held-lock and frame-transport
diagnostics also regressed and were retained locally rather than presented as
speedups. Host timing variation limits precision, and none of these figures is
a physical-iPad, ARM NEON or general application speed claim.

The final default-off decision preserves ordinary allocator routing while making
the implementation available for further work. Next work should reduce entry and
memory-adapter cost, improve original-source availability/refill coverage, then
add resumable cold kernel paths. It should retain musl's allocation strategy,
guest metadata ownership and original synchronization boundaries.

Exact-commit Linux/Apple CI is recorded separately when complete. No new release
tag or signed device build is produced by this development batch.

## Shared memory access follow-up

The next implementation makes worker native reads share the interpreter's
translation/permission/byte-access path, uses fixed-width allocator operands and
borrows safe local CLONE_VM memory once per native entry. See the
[shared memory contract](../architecture/native-allocator.md#shared-interpreternative-memory-access).
It also fixes the old standalone accelerator provenance test to locate the
newly vendored mallocng sources. That test-path error stopped Linux CI on
`fc7c084`; the same commit's Apple SDK builds and iPad simulator tests passed
in [run 38529011015](https://github.com/kb5000/trames/actions/runs/38529011015).

The follow-up passes 264 host workspace tests, 209 ARM64 tests under QEMU and
65 ThreadSanitizer tests, strict Clippy and the iOS compilation check. Allocator
matrix, parallel native/nested callbacks, full-musl upgrade/loader/TLS tests,
standalone accelerator entries and same-page independent CPU regions pass.
Four-CPU opt-in Node.js/npm additionally passes signed installation, default V8,
Atomics/workers, network, offline dependencies and retained-data reopening.
The ARM64 socket-lifetime test was rerun with sandbox network access enabled;
its first run was blocked by the sandbox rather than a guest-memory fault.

A separate paired batch compares allocator-enabled runtimes on both sides, so
the result measures the memory-adapter change instead of allocator activation.
Three alternating samples use identical guest ELF/libc/stores. Both binaries,
input hashes, all samples and instruction/native-body counts are recorded in
[the memory-access evidence](native-memory-data.json). The original binary is
`fc7c084`; the implementation is `986e1fb`. This fixture includes the later exec
probe, so its times must not be directly compared with the earlier prototype
fixture/table above.

| Workload | Previous adapter median | Shared access median | Previous/current ratio |
| --- | --- | --- | --- |
| Four allocation threads, 1 guest CPU | 1.754 s | 1.671 s | 1.05× |
| Four allocation threads, 4 guest CPUs | 23.365 s | 18.352 s | 1.27× |
| Normal Vim startup, 1 guest CPU | 2.195 s | 2.227 s | 0.99× |
| Python imports/SQLite/hash, 1 guest CPU | 9.453 s | 10.393 s | 0.91× |

The allocation medians improved in this batch, but four-CPU samples ranged from
16.55–26.38 s before and 15.91–24.14 s after. Python's current samples were
10.39, 42.07 and 9.03 s. That variation prevents a stable whole-system speedup
claim. Single-CPU allocation NativeCall body medians were 127.85→105.11 ms;
that counter covers all profiles and excludes interpreter/scheduler transitions.
Reducing body time alone cannot remove those transitions, repeated cold-path
fallback or original allocator contention. Default-off routing remains justified.
None of these shared x86_64-host measurements establishes iPad/ARM performance.

Exact-commit verification passed in
[Linux CI](https://github.com/kb5000/trames/actions/runs/38532793511) and
[Apple CI](https://github.com/kb5000/trames/actions/runs/38532793548).


## Batched scalar transport and scoped page reads

A further implementation moves the bounded write journal and overlapping-write
forwarding into generated native C. At CAS or return it transfers only the
initialized records; `MaybeUninit` avoids clearing the unused buffer. Checked
coordinator page borrows reuse translated addresses within a call, without Arc
clones or per-field permission checks. The native cache is invalidated before
any publication, and scalar read helpers are inlined. Parallel workers continue
using CPU scalar/atomic region access; no function-wide lock or additional
thread-safety guarantee is added. The original musl algorithms, guest ABI,
heap ownership, source cookie and default-off activation stay intact.

Local validation passes 267 host workspace tests, 212 ARM64/QEMU tests and
67 ThreadSanitizer tests, strict Clippy, ASan/UBSan and the iOS compilation
check. The eight allocator rows explicitly check direct-read callback reduction
at one CPU and ordinary load callbacks on parallel workers. Tests verify no
false COW, real fork COW, failed-CAS descriptor invalidation/slow continuation,
permissions, cross-page fault priority and shared-backing fallback. Full-musl
loader/startup/TLS/upgrades, all 19 parallel native functions and nested callbacks,
476 integer oracle rows, standalone stubs and independent CPU regions pass. Four-CPU opt-in Node.js/npm passes signed
installation, default V8, Atomics/workers, network, offline dependencies and
retained-data reopening. Functional commit: `10765d2`.

[Complete evidence](native-allocator-batch-data.json) retains three modes on
identical ELF/libc/stores: previous native runtime `37e99d6`, current native,
and current runtime with the original guest allocator. Mode order rotates
across three samples. The initial Arc-pin/out-of-line-read diagnostic is retained
separately; it reduced callbacks but did not improve time, prompting removal
of reference-count overhead and inlining. The following final medians are not a
stable overall performance claim:

| Workload | Previous native | Current native | Current original allocator |
| --- | --- | --- | --- |
| Allocation, 1 CPU | 1.500 s | 2.121 s | 1.441 s |
| Allocation, 4 CPUs | 16.243 s | 15.251 s | 11.815 s |
| Vim startup, 1 CPU | 2.090 s | 2.148 s | 1.973 s |
| Python imports/SQLite/hash, 1 CPU | 9.487 s | 9.155 s | 9.927 s |

Current one-CPU allocation samples were 3.818/2.121/1.500 s; original samples
were 23.689/1.441/1.424 s, and current four-CPU samples included 34.934 s.
These outliers and mode-dependent variability limit conclusions. Vim NativeCall
body medians improved from 68.12 to 60.27 ms, but its whole-startup median did
not improve. Do not report a blanket speedup or enable allocator routing by
default on this evidence.

The deterministic callback counts are more useful here: one-CPU allocation
records 1,859,389 direct field loads, 632,792 page resolutions and 571,210 staged
stores. The equivalent former field-load/store transport would cross C/Rust
2,430,599 times, compared with 632,792 read-page callbacks now, about 74% fewer;
CAS callbacks are excluded from both sides. No scalar load callback is needed
in that eligible one-CPU row. This is a transport comparison, not a wall-time
speedup. Original allocator contention, entry/return boundaries and repeated
cold-path fallback remain substantial costs. Measurements use the shared x86_64
development host and establish no physical-iPad or ARM performance claim.

Final functional-commit verification: [Linux CI](https://github.com/kb5000/trames/actions/runs/38545024356) and [Apple CI](https://github.com/kb5000/trames/actions/runs/38545024339) passed. No release/tag or signed-device version is added.
