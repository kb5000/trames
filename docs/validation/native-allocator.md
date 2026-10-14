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
