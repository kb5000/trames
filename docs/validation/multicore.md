# Host multicore execution: development validation

Date: 2026-10-13. Delivery tracker: [#11](https://github.com/kb5000/trames/issues/11).
This source change introduces real host CPU workers using the existing cached
AArch64 interpreter, with no JIT. The immutable 0.11.0/build 12 release is still
recorded separately in [release-0.11.md](release-0.11.md). Physical acceptance
remains in [#8](https://github.com/kb5000/trames/issues/8).

## Correctness and compatibility

The final local source passes 229 workspace tests, rustfmt and Clippy with all
features and warnings denied. Actual AArch64 compute/memory/CPU/VFS/core binaries
run under pinned QEMU 10.0.13 pass 154 tests, including two concurrent CPU workers
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
- CPU topology, enforced affinity, sysconf CPU counts and getcpu agree. Metrics
  verify actual overlapping worker execution, rather than only reporting CPUs.

Unit coverage additionally checks 16-byte reservation granules, ABA writes,
coordinator/native invalidation, remapped identities, permissions before effects,
shared aliases, readonly dirty-writeback aliases, file descriptor mutation,
truncation/SIGBUS and executable cache invalidation.

The full signed Node.js 22.23.2/npm 11.6.4 workflow is being verified with four
virtual CPUs, including three SharedArrayBuffer workers, Atomics.wait/notify and
3,000 exact atomic increments, default V8, libuv/fs/crypto, HTTP/fetch, child
processes, offline dependency installation and retained data after reopening.
Existing mmap/runtime-compat comparisons pass on both memory and disk backends.
Regenerated audits retain 893 instruction samples (877 decoded, two syscalls, two
synchronous exceptions, 12 unsupported) and 318 Linux entries (164 implemented
subsets, 152 missing, two obsolete), with all 154 undispatched numbers returning
ENOSYS on the actual ARM64 runtime. Exact-source Linux and native Apple CI references
will be recorded after publication.

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
(280,008,962 retired guest instructions). The memory control executes 13,000,020
instructions, with three subprocess runs averaged per sample. Runtime seconds,
wall seconds, host CPU consumption, binary/fixture hashes and source fingerprints
are retained in the raw report.

| Workload | Old cooperative runtime | New 1 CPU | New 2 CPUs | New 4 CPUs |
| --- | ---: | ---: | ---: | ---: |
| Four-thread compute, median seconds | 3.255 | 3.403 | 2.788 | 2.621 |
| Speedup against new 1 CPU | — | 1.00× | 1.22× | 1.30× |
| Single-thread memory, median seconds | 0.259 | 0.272 | — | — |

New single-CPU compute and memory medians are respectively 4.5% and 5.1% slower
than the old baseline. Multicore compute medians improve 14.4%/19.5% in elapsed
time against that baseline, but sample ranges overlap: four-CPU compute spans
1.982–3.574 seconds. Host CPU/wall ratios above one and worker overlap counters
establish actual parallel execution; they do not establish linear scaling or
universal application gains. No new Vim/Python/Node startup timing is claimed.

Preserved [initial](multicore-benchmark-initial.json) and
[intermediate](multicore-benchmark-intermediate.json) batches use shorter loops and
fewer memory repeats. The intermediate memory regression prompted CPU-scope
borrows and avoiding live code-epoch loads for immutable executable snapshots;
the final batch reports the remaining overhead instead of omitting regressions.
After the final timed batch, changes to Rust sources are a test and a public type
re-export; the musl correctness fixture subsequently gained peer exec/exit tests.
The raw timing fingerprints describe exactly the measured binaries/fixtures.

Mutable staging adds host memory beyond guest VmRSS/commit accounting. Short
slices, page contention, worker queues and serialized syscalls/native/backing
operations constrain speedup. Further optimization should reduce those costs
while preserving byte aliases, permissions/EOF, COW, atomicity and memory ordering.
Do not infer whole-system or physical-iPad acceleration from this fixture.
