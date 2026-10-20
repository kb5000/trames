# Sole-executor private guest memory

The scheduler now skips region acquisition/release for physically private pages
when a VM has exactly one dispatched CPU and no external coordinate/worker alias.
Different VMs can use this path simultaneously. CPU hardware/software access,
native fragments/fill/copy, atomic RMW and LL/SC use the same scoped ownership.
Private page versions and dirty/monitor masks avoid atomic RMW and extra
publication fences; the epoch/channel transfer publishes them with the bytes.

The [ownership proof](../architecture/region-memory.md#exclusive-private-epochs)
details actual root/owner counts, all-generation View counts and physical Rc/Arc
uniqueness. Permissions, EOF, quotas, fork COW, dirty commit, guest atomic/fence
semantics and global executable epochs remain enforced. Shared-VM threads,
shared/backed storage and ordinary coordinator access retain region guards.
This is an exclusive-owner fast path, not general lock-free shared memory.

## Fixed-work comparison

Command: `cargo run --release -p trames-memory --example private-memory -- 3`.
Baseline parent: `efba40df03f77a679440d10f816f4b0937da8d7a`.
[Raw output](private-memory/linux.log) and [samples/source hashes](private-memory/linux.json)
retain the complete measurement. Linux x86_64, AMD EPYC 9V45, 4096-byte host
pages, pinned nightly-2026-10-16, workspace release with thin LTO and overflow
checks. This is the **same binary** with unsafe private admission disabled/enabled,
three pairs with alternating order; table entries are medians.

Each worker owns a different VM with 128KiB materialized bytes. Translation and
residency warming, snapshot preparation and dirty commit are outside the timer;
barrier release/completion are inside. Scalar work performs ten million u64
read/increment/write pairs per worker over 16KiB. Native work performs two million
64-byte, or ten thousand 64KiB, fill-plus-copy pairs per worker. Checksums match
between modes in every sample; production has no benchmark-specific branch.

| Independent workers | Work | Guarded ms | Private ms | Ratio |
| --- | --- | ---: | ---: | ---: |
| 1 | scalar | 315.762 | 108.289 | 2.916x |
| 1 | native 64B | 135.166 | 73.671 | 1.835x |
| 1 | native 64KiB | 19.697 | 16.350 | 1.205x |
| 4 | scalar | 307.599 | 111.614 | 2.756x |
| 4 | native 64B | 152.350 | 77.393 | 1.969x |
| 4 | native 64KiB | 26.365 | 23.849 | 1.105x |

The large four-worker native case shows little improvement; memory traffic and
shared-host variation dominate this short sample. These timings isolate the
ownership/metadata cost. They do **not** establish shared-VM scaling or predict
Docker, Firefox, CoreMark, iPad or whole-app throughput. There is no confidence
interval. Apple ARM64/16KiB native execution is a separate CI measurement; it is
not inferred from these x86 results.

An additional isolated build of the unmodified parent is retained in
[baseline-linux.log](private-memory/baseline-linux.log), with the adapted example
source in the JSON manifest. It uses the same fixed work, libc, compiler and
release settings, with only the new admission/second mode removed. Its earlier,
separate batch is **not an interleaved controlled old/new comparison**. Parent
over final guarded throughput ratios for scalar/64B/64KiB are 0.936/0.947/0.979
with one worker and 0.984/0.867/1.047 with four. Below one is an unfavorable
guarded observation, including the four-worker 64B case; the private-path ratios
do not prove that every fallback is faster. The final guard retains its original
two-word size, with the mode in a provenance-preserving aligned pointer tag.
Shared-host noise, code layout and the extra mode selection remain relevant.

## Affected verification

- Memory: 83 tests pass locally, including five new exclusive-epoch cases. A
  test-only counter verifies that scalar reads/writes, native pair/move/fill,
  instruction fetch, atomic and exclusive operations perform no private-region
  acquisitions. Cases include cross-page/mixed-permission faults, ABA and adjacent
  monitor granules, native unwind, dirty/code publication and host-thread handoff.
- Shared mappings still use real locks while two independent private VMs operate
  concurrently. Fork writes stay separate; retained read-only fork Views remain
  guarded even after their coordinate owner is dropped.
- Core: 83 tests pass locally, including actual two-VM worker execution with exact
  instruction accounting, plus admission rejection for multiple CPUs in one VM,
  external coordinate aliases, retired process memories and external Views.
  Existing ready/disk I/O, native overlap and shared atomic worker tests pass.
- Strict Clippy for memory/core all targets and formatting pass. The CI plan
  checks affected memory/core/Linux/mapped-VFS paths, original-musl ARM64 sparse
  BSS and Apple ARM64/16KiB; adds fully instrumented std ThreadSanitizer on the new
  exclusive tests. Existing CPU opcode/GPU/UI/guest package sources are unchanged.
  This iteration does not build an IPA or claim Firefox first-frame acceptance.
