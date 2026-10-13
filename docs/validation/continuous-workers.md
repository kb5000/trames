# Continuous CPU workers

Development follow-up to 0.12.0/build 13; no new numbered device release.
Tracker: [#13](https://github.com/kb5000/trames/issues/13).
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

Verification and performance results are being collected. No physical iPad
timing or whole-application gain is claimed.

## Occasional performance batch

The preserved 0.12 runtime and candidate use explicit 1/2/4 CPU modes on the same
development host, in three alternating samples per mode. The fixed-work checksum
is compared with independent QEMU; exact guest retirement, source/binary hashes,
CPU time, wall time and new checkpoint metrics are retained. A single-thread
memory control uses three subprocess runs per sample. QEMU is the output oracle;
the timed Trames binaries run directly on x86_64, interpreting AArch64 guests.

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
