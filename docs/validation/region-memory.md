# Region memory and regression follow-up

Tracker: [Issue #17](https://github.com/kb5000/trames/issues/17).
Baseline `2b7759f8412a979d3194fd9a6cbbcaf516d43b1a` has the same runtime as
`6bbe185`. The saved release binary matches the preceding published benchmark's
SHA256 and every Rust source hash; it was copied before edits. Both runtimes use
the same guest ELF and complete 19-entry musl ELF. Delivered 0.13/build14 remains
unchanged. [Memory contract](../architecture/region-memory.md).

## Resulting behavior

Mutable CPU/native/atomic accesses guard only touched 64-byte regions. Disjoint
regions of one page can execute simultaneously; overlapping widths still use
one physical transaction domain. An audited UnsafeCell/Sync module exposes only
checked, guard-owned byte subranges. Guest races may yield different allowed
results; there is no invented race error or deterministic global schedule.
ARM synchronization/atomic constraints remain enforced, with a stronger subset
of allowed weak-memory outcomes rather than exhaustive weak-outcome simulation.

Full-page/full-region reservation bases avoid per-granule version writes while
preserving untouched 16-byte monitors and wrap. Dirty publication skips already
marked ranges in a joined epoch; contiguous dirty bits commit in one run, preserving
clean holes and backing aliases. Copy/admission checks share one mapping walk.
Built-in non-reentrant file/anonymous backings allow one local CLONE_VM borrow
per CPU slice; unknown providers retain their reentrant-read behavior.

## Verification

259 host workspace tests and 204 selected actual ARM64 tests under QEMU, strict
Clippy and iOS compilation cover the implementation. New tests hold a live
mutable slice while a peer accesses another region of the same page, serialize
overlap/unwind, stress atomic pairs without torn reads, preserve untouched
reservation granules, and verify one full backing write/coalesced sparse runs
without overwriting clean alias holes. The worker native test now requires peer
progress for both different pages and different regions within one page.

The custom readonly provider re-enters its containing Memory during check/read;
its test verifies the borrow optimization defaults to the existing per-access
path. The borrow ends before coordinated/native callbacks.

ThreadSanitizer is run with **std/core/alloc/test rebuilt and instrumented**:
`RUSTC_BOOTSTRAP=1 RUSTFLAGS=-Zsanitizer=thread cargo test -Zbuild-std -p trames-memory --target x86_64-unknown-linux-gnu --locked --lib`.
All 41 memory tests pass without reports or suppressions. An initial run against
uninstrumented std reported mpsc/Arc/test-capture synchronization warnings;
rebuilding std enables sanitizer-aware synchronization and removes those
reports. The final fully instrumented run is the acceptance evidence. This
dynamic check does not prove all possible executions race-free; the scoped
ownership invariant and ARM tests remain necessary.

Real musl tests include native pthread parsing/FP/errno/nested callbacks/shared
stacks/disk, SMP LSE/LL-SC/CASP/acquire-release/DMB/futex/fork/teardown/affinity,
712 floating and 476 integer oracle cases, full-musl startup/persistence, and
shared/private/anonymous mmap, EOF/truncate SIGBUS, COW/remap/seals/code aliases.
The new ordinary-access fixture increments four disjoint 64-byte counters in
one guest page, comparing exact output with original-musl QEMU at 1/2/4 CPUs.
Linux CI additionally runs full-std ThreadSanitizer and the actual ARM64 fixture.

Exact-source CI and downloaded simulator/source/runtime verification are
recorded below. No new physical-iPad acceptance or speed claim is made.

## Fixed-work performance

The [accepted-source batch](region-memory-benchmark.json) has three alternating
before/after samples per mode, four guest threads and identical binaries on both
runtimes. Copy does 512 iterations per thread over 1 MiB, **2 GiB total**, with
comparison/checksum work. The ordinary same-page program performs 400,000
increments per thread, with explicit guest ldr/add/str and no libc copy or guest
atomic RMW in the loop. The unchanged application control sorts 2,048 rows.

| Workload | CPUs | Before s | After s | Before/after |
| --- | ---: | ---: | ---: | ---: |
| Native copy, 2 GiB | 1 | 0.24054 | 0.19679 | 1.22x |
| Native copy, 2 GiB | 2 | 0.49228 | 0.36913 | 1.33x |
| Native copy, 2 GiB | 4 | 0.24429 | 0.20285 | 1.20x |
| Same-page ordinary counters | 1 | 0.19299 | 0.19367 | 1.00x |
| Same-page ordinary counters | 2 | 0.40199 | 0.37267 | 1.08x |
| Same-page ordinary counters | 4 | 0.52377 | 0.46239 | 1.13x |
| BusyBox sort | 1 | 0.36143 | 0.35058 | 1.03x |

The preceding small 512 MiB copy regression is not directly compared with this
longer workload. The same-source before/after control shows reduced single-CPU
copy cost and approximately flat ordinary single-CPU access. Four-CPU copy still
does not exceed the direct single-CPU path (0.97x), and four-CPU ordinary counters
remain substantially slower than that direct path. Mask/version cache traffic,
worker transaction cost and scheduling still limit scaling.

Two diagnostic batches preceded the accepted-source batch, each followed by a
concrete fix: [first region implementation](region-memory-initial-benchmark.json)
exposed a roughly 10% ordinary single-CPU regression, motivating the explicit
non-reentrant slice borrow; [borrow refinement](region-memory-borrow-benchmark.json)
retains noisy/regressed two-CPU copy samples before redundant dirty RMWs and
whole-VM mapping admission scans were removed. They are diagnostic fingerprints,
not the final source's performance claim. No further timing batches followed the
frozen accepted implementation.

Two-CPU timings are particularly variable: accepted copy baseline samples span
0.353–0.504 s and current samples 0.262–0.423 s; the borrow-refinement batch has
the opposite median direction. These short hosted measurements cannot establish
a universal two-CPU improvement. Sort's small change also stays qualified.

Runtime seconds exclude rootfs import. Host child user/system CPU and wall time
include import/startup; summed worker `cpu_seconds` includes lock waits and is
not host CPU utilization. This x86_64 development executor interprets ARM64
guests under a four-core cgroup quota; NEON/iPad hardware speed is not measured.

## Exact-source acceptance

Runtime source [`dd492dd`](https://github.com/kb5000/trames/commit/dd492dde61523503533de0c57b356be969735e3c)
passes [Linux 38512362267](https://github.com/kb5000/trames/actions/runs/38512362267)
and [Apple 38512362055](https://github.com/kb5000/trames/actions/runs/38512362055)
on attempt 1. [Run/job evidence](region-memory-ci.json) records all required steps;
the actual Linux transcript confirms host 259, full-std sanitizer 41 with zero
reports, and actual ARM64 204 tests. Apple native core, SSH/SFTP, device/simulator
link, real gateway DNS/HTTPS and lifecycle steps pass at this same SHA.

The downloaded [simulator report](region-memory-simulator.json) records **7 passed,
0 failed, 0 skipped**. [Artifact verification](region-memory-artifacts.json)
checks ZIP SHA256/CRC, both app GuestMusl ELF/manifest files against the paired
19-entry library/source artifact, all 17 adapted/original input hashes, original
upstream archive and GPL/MIT notices. The [full source archive](https://github.com/kb5000/trames/archive/dd492dde61523503533de0c57b356be969735e3c.tar.gz)
preserves the audited host boundary and tests alongside the guest source package.

Downloaded runtime evidence includes actual ARM64 [native pthread results](region-memory-arm-pthread.json),
[same-page ordinary CPU transcript](region-memory-ci-region-memory-workflow.txt),
[native transcript](region-memory-ci-native-parallel-workflow.txt),
[SMP transcript](region-memory-ci-smp-workflow.txt) and
[Node transcript](region-memory-ci-nodejs-workflow.txt). Checks cover 1/2/4 CPU
ordinary counters, all native entries/shared stacks/disk, affinity/futex/ARM
publication, Node Atomics/offline npm/reopen and GCC/Clang/Make/CMake/CTest markers.
The native/region fixtures use actual ARM64 Trames under QEMU; Node and developer
tools use the development host interpreting ARM64 guests.

This validates the development source. It does not replace the immutable
delivered 0.13/build14 IPA or establish new physical-device performance.
