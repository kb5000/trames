# Host multicore execution: development validation

Date: 2026-10-13. Initial implementation: `0b19861a2d8ba40d79a30af8054d52f982f855f2`.
The final follow-up corrects affinity fairness and uses explicit Bash pipefail for CI evidence.
Delivery tracker: [#11](https://github.com/kb5000/trames/issues/11).
This source change introduces real host CPU workers using the existing cached
AArch64 interpreter, with no JIT. The immutable 0.11.0/build 12 release is still
recorded separately in [release-0.11.md](release-0.11.md). Physical acceptance
remains in [#8](https://github.com/kb5000/trames/issues/8).

## Correctness and compatibility

The final local source passes 230 workspace tests, rustfmt and Clippy with all
features and warnings denied. Actual AArch64 compute/memory/CPU/VFS/core binaries
run under pinned QEMU 10.0.13 pass 155 tests, including two concurrent CPU workers
and independent per-host-thread floating environments. Core/distro/FFI also pass
an aarch64-apple-ios target check; this is not an SDK link or device test.

The static musl pthread fixture agrees with independent Linux/QEMU at interpreter
CPU counts 1, 2 and 4, on both development-host and actual AArch64 runtime builds:

- Four threads update counters using LSE, LL/SC, CASP and paired exclusives;
  TLS and errno retain per-thread values.
- Release/acquire publication spans separate pages. A DMB store-buffering test
  rejects the synchronized forbidden outcome; this does not exhaust ARM litmus
  behavior or require every weak outcome to appear.
- Futex compare/enqueue/wake races do not lose notifications. Shared anonymous
  futex keys work across fork and backing identities, beyond one virtual address.
- Concurrent fork preserves private COW while other tasks write; shared-process
  atomics remain coherent. Exec and exit_group retire a running peer at a joined
  checkpoint before replacing or removing its CPU/address-space state.
- Two pinned spinners wait for a flexible task without starving it. CPU topology,
  enforced affinity, sysconf CPU counts and getcpu agree. Metrics
  verify actual overlapping worker execution, rather than only reporting CPUs.

Unit coverage additionally checks 16-byte reservation granules, ABA writes,
coordinator/native invalidation, remapped identities, permissions before effects,
shared aliases, readonly dirty-writeback aliases, file descriptor mutation,
truncation/SIGBUS and executable cache invalidation.

The final full signed Node.js 22.23.2/npm 11.6.4 workflow passes with four
virtual CPUs, including three SharedArrayBuffer workers, Atomics.wait/notify and
3,000 exact atomic increments, default V8, libuv/fs/crypto, HTTP/fetch, child
processes, offline dependency installation and retained data after reopening.
Existing mmap/runtime-compat comparisons pass on both memory and disk backends.
Regenerated audits retain 893 instruction samples (877 decoded, two syscalls, two
synchronous exceptions, 12 unsupported) and 318 Linux entries (164 implemented
subsets, 152 missing, two obsolete), with all 154 undispatched numbers returning
ENOSYS on the actual ARM64 runtime. Exact-source Linux and native Apple CI evidence is recorded below.

See [architecture](../architecture/multicore.md) for ownership, fence, backing,
code-token and checkpoint contracts. Ordinary guest accesses may race; page-local
host transactions prevent Rust data races and provide a stronger subset of ARM's
allowed ordering. Kernel/NativeCall remain serialized at joined checkpoints.

## Occasional fixed-work benchmark

The [final raw batch](multicore-benchmark.json) alternates baseline and candidate
modes with three samples per mode. The baseline source is
`3744ad66fd3950a9875ef5ac7c50cfc9769d8b20` (the verified 0.11 runtime plus docs),
built in a detached baseline worktree. All output checksums match independent
QEMU. This is an x86_64 development host, with a four-core cgroup quota and five
CPUs in the process affinity mask; the guest remains AArch64. It is not a physical
iPad measurement. Rust 1.90.0 release/all-features and one cached interpreter are
used throughout.

Four guest threads each execute 10,000,000 fixed integer-compute loop iterations
(280,008,963 retired guest instructions). The memory control executes 13,000,020
instructions, with three subprocess runs averaged per sample. Runtime seconds,
wall seconds, host CPU consumption, binary/fixture hashes and source fingerprints
are retained in the raw report.

| Workload | Old cooperative runtime | New 1 CPU | New 2 CPUs | New 4 CPUs |
| --- | ---: | ---: | ---: | ---: |
| Four-thread compute, median seconds | 3.116 | 3.405 | 2.439 | 2.038 |
| Speedup against new 1 CPU | — | 1.00× | 1.40× | 1.67× |
| Single-thread memory, median seconds | 0.282 | 0.246 | — | — |

New single-CPU compute is 9.3% slower than the old baseline in this batch;
single-thread memory is 12.7% faster. Multicore compute medians reduce elapsed
time 21.7%/34.6% against that baseline. Four-CPU compute spans 1.922–2.064 seconds,
while the memory baseline spans 0.250–1.081 seconds and overlaps candidate samples.
Host CPU/wall ratios above one and worker overlap counters establish actual
parallel execution; they do not establish linear scaling or universal application
gains. No new Vim/Python/Node startup timing is claimed.

Preserved [initial](multicore-benchmark-initial.json),
[intermediate](multicore-benchmark-intermediate.json) and
[before-affinity-fix](multicore-benchmark-before-affinity.json) batches expose the
variation and regressions. The shorter intermediate batch's memory regression
prompted CPU-scope borrows and avoiding live code-epoch loads for immutable code.
The before-affinity batch measured 1.22×/1.30× scaling and a 5.1% memory regression;
the final repeat follows the correctness fix for pinned-task starvation. Do not
attribute the differences between cloud batches to that scheduler fix alone.
The final raw fingerprints match the tested Rust source and measured fixture;
there are no subsequent runtime edits in this validation follow-up.

Mutable staging adds host memory beyond guest VmRSS/commit accounting. Short
slices, page contention, worker queues and serialized syscalls/native/backing
operations constrain speedup. Further optimization should reduce those costs
while preserving byte aliases, permissions/EOF, COW, atomicity and memory ordering.
Do not infer whole-system or physical-iPad acceleration from this fixture.

## Exact-source continuous integration

Final runtime source: `912b04059866c6d8cc739cf53d9b1ca848d1c6c1`.
Both workflows execute this SHA; the final evidence-only commit changes no runtime
source. The initial implementation `0b19861a2d8ba40d79a30af8054d52f982f855f2` also
passed both complete workflows on their first attempts, before affinity hardening.

- [Linux runtime, run 38369325408](https://github.com/kb5000/trames/actions/runs/38369325408):
  success on attempt 1, job `115202085478`. Workspace/target checks, all instruction
  comparisons, musl/NativeCall, mmap, signals, network, signed Python, actual
  ARM64 pthread SMP at 1/2/4 CPUs, forced-four-CPU Node/npm, and retained
  Git/GCC/Clang/Make/CMake workflows pass.
- [Apple, run 38369325421](https://github.com/kb5000/trames/actions/runs/38369325421):
  success on attempt 1, Apple job `115202533803`. Native ARM64 compute/CPU/memory/
  VFS/core/native/accelerator tests, actual OpenSSH PTY/SFTP, simulator/device links
  and seven iPad simulator tests pass (zero failures).

The [Linux runtime evidence archive](https://github.com/kb5000/trames/actions/runs/38369325408/artifacts/11799670006)
contains five transcripts. Artifact `11799670006` belongs to the exact runtime SHA;
its downloaded 3,690-byte ZIP passes CRC and SHA-256 verification:
`82abaebe455792f681fa457ea64f126ff1557331e82ac8ba92d7bbe5651cb612`.
The [SMP transcript](multicore-ci-smp.txt) is retained in the repository independently
of artifact expiry. All promised output markers, including pinned-task fairness,
shared Atomics, offline npm and retained compiled/owner data, were inspected.

The [Apple build/project evidence](https://github.com/kb5000/trames/actions/runs/38369325421/artifacts/11799296734)
archive belongs to the same runtime SHA. Its 33,338,679-byte ZIP passes CRC and
SHA-256 verification:
`54ea78f69d2fee65e6e36227c692b7b3bacdf79e1bac148c8dff7733c5ccbd55`.
The [actual simulator summary](multicore-ipad-summary.json) is retained locally in
the repository: seven passed, zero failed. Full decoded Apple job logs could not
be fetched because the connector closed the transport; the completed job/step
metadata and downloaded simulator report are the inspected evidence.
These are development builds, not a new numbered device release or a physical
iPad multicore/performance acceptance. The next signed device update remains a
grouped delivery; no runtime change follows the verified source in this record.
