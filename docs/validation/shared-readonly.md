# Immutable private pages in a shared VM

The scheduler admits all selected Views of the same frozen address space as one
group. Its physically private RX/R pages can be read without region locks by
multiple CPUs and pure native functions. Single-executor private read/write
admission remains. Writable pages, shared file/anonymous mappings, live private
file shadows, retained fork storage and ordinary coordinator access keep their
existing synchronization. No additional CPU backend or JIT is introduced.

The [ownership proof](../architecture/region-memory.md#immutable-private-pages-in-a-shared-vm)
requires every coordinate handle, all-generation View counts and identical
Snapshot Arcs. It distinguishes a read-only VMA from immutable physical storage.
Per-operand selection is a view-mode/frame-access bit mask. No new refcount load,
permission walk or fence is added. The 16-byte scoped guard uses two aligned
pointer tag bits with provenance preserved; read-only guards only read bytes and
monitor rows and perform no unlock RMW.

## Affected verification

- Memory88/Core83 pass locally; strict Clippy for memory/core all targets and
  formatting pass. Four new readonly tests and one page-guard test cover four
  simultaneous borrows of the **same** private page, reentrant immutable reads,
  hardware/software/native reads, fetch, pair reads and copying to writable
  destinations. A test-only counter verifies no region acquisition for immutable
  frames. Four writers still make exactly 4,000 shared atomic increments.
- LDXR remains valid on R mappings: parallel first reads initialize monitor rows
  through OnceLock, then read immutable rows. Write/atomic/STXR attempts on R
  mappings fail their original permission check. Joined R-to-RW transitions keep
  ABA/neighbor-granule invalidation; native unwind ends its borrow correctly.
- Missing/external Views, different snapshot generations and external coordinate
  handles reject admission. Shared backing and retained other-VM fork pages stay
  guarded, including after their coordinate owner is dropped. Read-only guard
  drop cannot release another held region; guard size remains 16 bytes.
- Actual CPU-worker execution reuses the shared atomic guest-loop fixture and
  exact instruction budget, now also asserting shared-readonly admission. No
  unrelated CPU opcode/GPU/UI/guest-package inputs change.
- CI checks affected memory/core/Linux/mapped-VFS, original-musl ARM64 sparse BSS,
  Apple ARM64/16KiB and 21 fully instrumented std/core/alloc/test TSan cases.
  TSan checks native/scoped Rust bytes and metadata; raw scalar asm itself is not
  instrumented. The previous storage and exclusive-private benchmark samples are
  reused; this iteration measures shared immutable reads and the RW control.
  No IPA or Firefox first-frame acceptance is claimed here. Current CI results
  and native ARM64 measurements are recorded in [issue #23](https://github.com/kb5000/trames/issues/23).

## Fixed-work comparison

`cargo run --release -p trames-memory --example shared-readonly -- 3`

Parent: `a74d758929f2d1fabfc2bc62283b83bf9f4b2eb5`. Linux x86_64, AMD EPYC 9V45,
4096-byte pages, pinned nightly-2026-10-16, workspace release with thin LTO and
overflow checks. [Raw samples](shared-readonly/linux.log) and
[source hashes/structured results](shared-readonly/linux.json) retain all pairs.
The same binary enables/disables group admission. Three pairs alternate order;
the table reports medians. Preparation, warming and commit are outside the timer;
start barrier/completion are inside. All checksums match.

Every worker uses the same address space and 64KiB readonly source. Scalar work
performs ten million u64 reads per worker over 16KiB. Native work performs five
million 64-byte pair comparisons per worker within one page. The RW control
performs five million read/increment/write pairs per CPU, on distinct writable
pages of that same VM. Those pages retain normal guards in both modes.

| CPUs sharing one VM | Work | Guarded ms | Readonly mode ms | Ratio |
| --- | --- | ---: | ---: | ---: |
| 2 | scalar reads | 176.566 | 67.064 | 2.633x |
| 4 | scalar reads | 432.458 | 69.857 | 6.191x |
| 2 | native pair 64B | 255.549 | 49.231 | 5.191x |
| 4 | native pair 64B | 721.727 | 71.652 | 10.073x |
| 2 | writable control | 174.339 | 174.311 | 1.000x |
| 4 | writable control | 179.149 | 174.527 | 1.026x |

This intentionally isolates contended read locks; the larger ratios reflect
removing inter-core held-mask traffic, not an all-program scaling factor. The RW
control shows no material difference in this batch, but is not a separately
built old-runtime comparison. Shared-host noise and code layout still matter;
there is no confidence interval. These are not CoreMark, Docker, Firefox or
physical iPad measurements. Native Apple ARM64 evidence is recorded separately
rather than inferred from the x86 results.
