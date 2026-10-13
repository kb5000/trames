# Continuous CPU workers

Development follow-up to 0.12.0/build 13; no new numbered device release.
Tracker: [#13](https://github.com/kb5000/trames/issues/13).
Final runtime source: [`7eebd3d`](https://github.com/kb5000/trames/commit/7eebd3de4c93db214e1d45481cb42d0d9c0d626b).
Baseline source: `2339dd1c445d1e57738e988273ed076420dee317`.
The [0.12.0 delivery](release-0.12.md) and [original multicore batch](multicore.md)
remain immutable evidence for their own source revisions.

## Behavior

The old runtime returned all workers after at most 32,768 instructions. Workers
now keep their CPU, decode state and checked memory view across local instruction
polls. A completed 16,384-instruction poll continues immediately without a
coordinator message or a peer barrier. A guest kernel/native/fault/yield event
requests a global checkpoint; timed checkpoints retain control, timer and affinity
fairness. Kernel, backing changes and NativeCall remain coordinator-owned and
execute only after every worker has returned. There is one cached interpreter
and no JIT. [Ownership and memory-order contracts](../architecture/multicore.md).

Shared exact budget credits prevent concurrent workers from overspending a session
limit. Pending native traps reserve their possible retirement before returning;
faults include all earlier local polls. Cancellation and pool teardown request a
stop and drain active work. Expired unread timerfd readiness does not starve CPUs.
Only an active poll/epoll wait supplies a poll deadline. Failed waits clear their
deadline and restore their temporary signal mask, so later read/poll calls and
runnable or stopped tasks cannot inherit a stale scheduling deadline.

CLI metrics retain `worker_slices` as dispatched worker assignments and add
`worker_chunks` for local polls. `parallel_rounds` now counts joined checkpoints,
not every completed instruction chunk. `dispatch_seconds` measures sending work;
`stop_seconds` measures draining peers after the first result or timeout. Neither
is a complete estimate of scheduler/locking overhead. `memory_seconds` continues
to cover preparing and committing views, excluding worker-side page transactions.
Worker peak counters exclude assignments canceled before executing instructions.

## Verification

New tests check uninterrupted work beyond several former quanta with an unused
peer slot, non-divisible shared budgets, native-trap credits, multi-poll faults,
endless-worker cancellation/teardown, and timer deadlines. Existing real musl SMP
fixtures cover LSE/LL-SC/CASP/pair operations, release/acquire publication, DMB,
TLS/errno, shared futexes, concurrent fork, running-peer exec/exit and affinity
fairness. The integration harness additionally compares sustained four-thread
fixed work with independent QEMU at 1/2/4 CPUs, without a timing assertion.

Local validation passes 238 workspace tests, 183 tests of the actual ARM64
compute/native/CPU/memory/accelerator/VFS/core binaries under pinned QEMU,
rustfmt, strict Clippy and iOS compilation. Real SMP and sustained-work
comparisons pass at 1/2/4 CPUs on both the development-host executor and actual
ARM64 Trames. Runtime-compat and mmap pass memory/disk combinations in both
environments. Complete hybrid musl passes all 16 startup/runtime/callback
combinations and standard-path persistence/rollback. Signed Node.js 22.23.2/npm
11.6.4 passes with four CPUs, including SharedArrayBuffer Atomics workers,
libuv/fs/crypto, HTTP/fetch, child processes, offline npm and retained data.
No physical iPad timing or whole-application gain is claimed.

## Occasional performance batch

The preserved 0.12 runtime and candidate use explicit 1/2/4 CPU modes on the same
development host, in three alternating samples per mode. The fixed-work checksum
is compared with independent QEMU; exact guest retirement, source/binary hashes,
CPU time, wall time and new checkpoint metrics are retained. A single-thread
memory control uses three subprocess runs per sample. QEMU is the output oracle;
the timed Trames binaries run directly on x86_64, interpreting AArch64 guests.

| Fixed workload, median seconds | 0.12 baseline | Continuous workers | Baseline/current |
| --- | ---: | ---: | ---: |
| Four guest threads, 1 CPU | 3.483 | 3.498 | 0.996× |
| Four guest threads, 2 CPUs | 2.381 | 1.726 | 1.380× |
| Four guest threads, 4 CPUs | 1.831 | 1.053 | 1.738× |
| Single-thread memory, 1 CPU | 0.276 | 0.250 | 1.107× |

Four-CPU elapsed time drops 42.5% against the preserved same-CPU baseline.
Within this batch, baseline 4/1 scaling is 1.902× and candidate scaling is
3.321×. Two-CPU candidate scaling is 2.027×, exposing host variation rather
than proving superlinear scaling. Single-CPU compute is 0.4% slower by median;
single-thread memory is 9.7% faster. These observations do not establish a
single-thread benefit from the parallel-path change.

Ranges for baseline/current compute are 3.393–3.935 / 3.377–3.772 (1 CPU),
2.312–2.495 / 1.484–2.455 (2 CPUs), and 1.732–3.344 / 1.032–1.072 (4 CPUs).
Memory ranges are 0.259–0.280 / 0.245–0.260 seconds. All samples are retained;
there is no timing-based acceptance test.

Four-CPU checkpoints fall from exactly 2,154 per sample to 124/118/112;
the candidate executes 16,482–17,091 local polls without scheduler handoffs.
Mean host CPU occupancy across each run rises from 1.564–2.468 to 3.227–3.281
cores. This includes coordinator CPU consumption. Candidate dispatch takes
13.3–20.8 ms; draining peers takes 29.8–73.8 ms, reflecting host scheduling
variation as well as bounded interrupt polling. Preparing/committing views takes
8.8–11.3 ms, excluding worker-side page locking. These counters do not partition
all costs or establish a universal CPU utilization improvement.

Each guest performs the same 40 million loop iterations and matches QEMU's
checksum. The original runtime and candidate 1-CPU samples retire 280,008,963
guest instructions. Candidate 2/4-CPU samples retire 280,007,145–280,008,963;
startup/synchronization paths depend on scheduling. The largest difference is
1,818 instructions, under 0.001% of the total. Every memory control retires
13,000,020 instructions. [Raw samples and source/binary fingerprints](continuous-workers-benchmark.json)
retain these distinctions. Host quota is four CPUs, affinity permits five;
this is a shared x86_64 development-host result, not physical iPad performance.

The [initial batch](continuous-workers-benchmark-initial.json), on `6432590`,
is also retained: four-CPU median 1.792 → 1.135 seconds and 2.719× candidate
4/1 scaling, with a **7.2% slower** single-thread memory control. The second batch
followed the inactive-deadline fix on measured source
`c0d3d09eee4bc6cb100fd8c23a68f64a6e45bb80`; all source/binary fingerprints name
that revision. Final source `7eebd3d` additionally finishes failed poll waits and
restricts deadlines to poll syscalls; its continuous worker loop is unchanged.
That wait-cleanup follow-up has correctness validation but no additional timing
batch. The two batches are not combined, and their single-thread variation is
not presented as a stable gain or regression.

```sh
python3 tests/performance/smp.py \
  --baseline /path/to/preserved-0.12-trames-run \
  --baseline-commit 2339dd1c445d1e57738e988273ed076420dee317 \
  --baseline-cpus 1 2 4 --output /tmp/continuous-workers-benchmark.json
```

Remaining limits include joined kernel/native operations, page-local mutable
transactions, host scheduling variation and periodic fairness/control checkpoints.
Continuous execution removes per-chunk coordination; it does not imply linear
scaling or independently concurrent Linux syscall execution.

## Hosted validation

[Linux CI](https://github.com/kb5000/trames/actions/runs/38386157698) passes on
final source `7eebd3d`, on attempt 1. It includes the complete instruction
differentials, musl/NativeCall/memory/network comparisons, actual ARM64 SMP,
signed Python, four-CPU Node/npm, and retained Git/GCC/Clang/Make/CMake workflows.
The downloaded runtime-evidence artifact `11808025595` contains five transcripts;
its 4,061-byte ZIP passes CRC and API SHA-256 verification:
`dc842500a9f7e441eff62b265e0f3bcdeb2171769b7dac0f2ef760aa09d9bdc7`.
All required SMP, shared Atomics, offline npm, persistence and developer-tool
markers were inspected. [Actual ARM64 SMP transcript](continuous-workers-smp.txt)
and [Node/npm transcript](continuous-workers-nodejs.txt) are retained independently
of artifact expiry.

[Apple CI](https://github.com/kb5000/trames/actions/runs/38386157824) passes on
final runtime source `7eebd3de4c93db214e1d45481cb42d0d9c0d626b`, on attempt 1.
Native ARM64 component/core tests, actual OpenSSH PTY/SFTP, simulator and device
links all pass. The downloaded [actual iPad simulator report](continuous-workers-simulator.json)
records seven passed tests, zero failed and zero skipped on iPad Pro 11-inch (M4),
iOS 18.5 ARM64. Build artifact `11806494322` has 33,334,591 bytes; downloaded
ZIP CRC and SHA-256 match its API digest:
`2f21cf240ae063c0d4138baa2b1064f214bbba7817995f1ee644557e7645ee8c`.
This development build is separate from the numbered 0.12.0 delivery; no new
physical iPad installation or measurement is claimed.

The initial implementation on `6432590` also passed
[Linux CI](https://github.com/kb5000/trames/actions/runs/38381979859) and
[Apple CI](https://github.com/kb5000/trames/actions/runs/38381979830), both on
attempt 1; its [simulator summary](continuous-workers-simulator-initial.json)
is retained separately. Subsequent correctness fixes have their own final-source
validation rather than reusing that earlier source claim.

[Exact-source CI jobs and steps](continuous-workers-ci.json) and
[validation fingerprints and downloaded-evidence checks](continuous-workers.json)
retain the final revision and measurement distinctions. The next device update
remains grouped; the immutable 0.12.0/build 13 package is still the delivered IPA.
