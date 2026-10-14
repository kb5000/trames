# Sparse parallel commit and CPU dispatch

Follow-up to the native allocator measurements, tracked in
[Issue #18](https://github.com/kb5000/trames/issues/18). Baseline `50d2b82` uses
the same functional runtime as `10765d2`; its release binary was saved before
edits and matches the preceding batched-allocator evidence. Guest musl and all
guest fixture binaries are unchanged. The delivered 0.13.0/build 14 stays fixed.

## Change and boundaries

Worker CPU/native writes mark a sparse bitmap of stable mapped-frame IDs.
Joined publication scans one word per 64 frame slots and only visits marked
pages. Physical 64-byte dirty fragments still determine the exact backing runs;
clean alias holes are preserved. Cached views retain the bitmap across backing
epoch refreshes; mapping changes require joined workers and rebuild it. Failed
backing writes retain pending fragments and frame bits for retry.

The coordinator commits each shared VM once before processing returned events.
Fork spaces stay separate, including shared physical file aliases. Decode-slot
allocation becomes lazy, avoiding a 128-slot allocation/free for each dispatched
CPU placeholder. Initialization runs once at the slice entry, outside hot page
lookups. Warm CPU caches continue moving with the guest CPU.

See the [memory contract](../architecture/region-memory.md). Guest synchronization,
atomic/exclusive semantics, mapping checkpoints, instruction budgets and original
musl algorithms remain in place. This batch does not remove syscall-driven global
checkpoints or native allocator cold-path fallback. Experimental allocator routing
still defaults off.

## Correctness

- 269 host workspace tests and 214 distinct ARM64 tests under QEMU passed.
- 112 memory, accelerator and core tests passed ThreadSanitizer with Rust
  std/core/alloc/test rebuilt and instrumented, without suppressions or reports.
- Strict workspace Clippy, formatting and iOS target compilation passed locally.
  The compilation check is not an SDK-linked app or device test.
- New deterministic tests cover sparse writes across bitmap words, recycled
  root-frame indices, repeated epochs, retained earlier views after backing epoch
  changes, and recovery from a failed backing write. Existing tests cover aliases,
  COW, executable epochs, reservation ABA, exact budgets and worker cancellation.
- Eight original/native allocator rows and seven native pthread rows passed,
  including 1/2/4 CPUs, all 19 pure entries, nested callbacks, shared stacks and
  disk persistence. SMP and ordinary same-page counters matched independent QEMU.
- mmap integration passed shared/private/anonymous maps, fork, EOF/SIGBUS,
  reservations, COW discard, remap, memfd/seals and code alias invalidation.
  Full-musl upgrade/startup/TLS/pipe/exec/persistence workflows passed.
- Four-CPU opt-in allocator Node.js 22.23.2/npm 11.6.4 passed signed installation,
  filesystem/JSON/Intl/crypto/zlib/timers, Atomics, workers/child processes,
  DNS/HTTP/fetch/virtual egress, offline install and retained-data reopening.

Host/ARM socket tests were rerun with sandbox network access enabled after initial
environment permission failures. mmap explicitly used the saved final release
runtime; the default debug executable was an older build.

## Fixed-work comparison

The [complete evidence](checkpoint-cost-data.json) records one alternating batch
of three samples per baseline/current mode, with identical ELF/libc and cloned
stores. The script is [checkpoints.py](../../tests/performance/checkpoints.py).
It includes default allocator routing at 1/4 CPUs, explicit native routing at
4 CPUs, a 2 GiB native-copy workload, four same-page counters with 400,000
increments each at 1/4 CPUs, normal Vim startup and Python imports/SQLite/hash.

| Workload | CPUs | Before s | After s | Before/after |
| --- | ---: | ---: | ---: | ---: |
| Original allocator | 1 | 1.686 | 1.521 | 1.108x |
| Original allocator | 4 | 11.758 | 10.162 | 1.157x |
| Experimental native allocator | 4 | 17.200 | 14.176 | 1.213x |
| Native copy, 2 GiB | 4 | 0.224 | 0.242 | 0.923x |
| Same-page ordinary counters | 1 | 0.210 | 0.214 | 0.984x |
| Same-page ordinary counters | 4 | 0.436 | 0.487 | 0.894x |
| Normal Vim startup | 1 | 3.171 | 2.333 | 1.359x |
| Python imports/SQLite/hash | 1 | 9.132 | 9.639 | 0.947x |

Four-CPU original allocator memory prepare/commit medians decrease from
3.673 to 2.573 s; per-round medians decrease from 66.16 to 48.48 microseconds
(about 27%). Native allocator decreases from 4.441 to 3.105 s, or 58.11 to
39.87 microseconds per round (about 31%). The respective checkpoint counts
remain about 55,519/53,080 and 76,431/77,873. This targets repeated checkpoint
work; syscall-triggered global stops and allocator entry/fallback remain.
These field medians are independent statistics, not a summed decomposition.

The complete evidence also retains the preceding diagnostic binary and source
fingerprints. Its single-CPU counter median regressed about 6%, prompting movement
of the lazy-cache initialization check from hot page lookups to slice entry.
The final counter median is about 1.7% slower; these samples do not prove that
the relocation alone caused that change. Diagnostic copy/counter four-CPU ratios
were 1.224x/1.162x, reversing direction in the final batch. The bitmap also adds
a relaxed marker test to worker writes, which can cost time in tight loops.
Those regressions are retained rather than excluded as noise.

Original four-CPU allocation samples span 10.919–15.615 s before and
9.942–11.496 s after. Native current has a 24.969 s outlier. Vim baseline samples
are 2.517–3.691 s in the final batch, versus 2.032–2.270 s in the diagnostic;
Python baseline includes 14.154 s. The sign reversals, outliers and small sample
count prevent stable whole-application or general-system speed claims. Next
work should reduce per-store publication overhead and syscall-driven stops;
the native allocator remains experimental and default off.

Measurements use the shared x86_64 development host. Runtime seconds exclude
rootfs import; Vim measures process spawn to its first normal visible cursor.
Worker `cpu_seconds` and native-body time sum elapsed intervals across workers,
including waits, and overlap coordinator wall-time measurements. They cannot
be added to reconstruct a total or establish host CPU occupancy. Raw child
user/system CPU and host wall times are retained separately. These data establish
no physical-iPad/ARM performance result or timing guarantee.

Exact-source Linux/Apple CI will be recorded after publication. No new release,
tag or signed-device version is produced.
