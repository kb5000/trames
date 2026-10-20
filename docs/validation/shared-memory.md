# Shared physical guest memory

The coordinator and all CPU/native views of a VM now use the same resident byte
allocation. Mapping snapshots contain immutable metadata and ownership handles;
they are not RAM snapshots. Guest CLONE_VM and additional worker views cause no
COW. Genuine private fork/COW remains distinct.

## Reproducible storage comparison

Baseline: `145db7629ce29fd1780619f20dbffee3a24418b9`; measured shared-source build:
`be259742b0b9281f19619cd905a2248c1aa711f4`. Linux x86_64, 4KiB host
pages, pinned nightly-2026-10-16, workspace release profile (`lto="thin"`,
`overflow-checks=true`). The baseline was built from an isolated copy of the
original memory crate, with exactly the same new example and release profile.
The new build uses the production workspace crate.

```
cargo run --release -p trames-memory --example shared-memory -- 128
```

The example materializes 128MiB, publishes one VM and four reader views, touches
every page through each view, stores one u64 per page, joins the views and
commits. The three alternating before/after runs are in
[comparison.json](shared-memory/comparison.json); values below are medians.
These views exercise publication and aliases, not parallel CPU scaling.

| Metric | Before | Shared source |
| --- | ---: | ---: |
| Host PSS after publication | 275.60 MiB | 148.23 MiB |
| Source pages copied into worker storage | 32,768 | 0 |
| Copied publication bytes | 128 MiB | 0 |
| Publication | 95.591 ms | 49.659 ms |
| Page stores | 1.984 ms | 1.840 ms |
| Joined dirty commit | 2.703 ms | 0.578 ms |

The PSS reduction is 127.37MiB (46.2% of process PSS), approximately one full
128MiB shadow allocation, with slightly different metadata. Commit is about
4.7x faster in this workload. Timings are microbenchmarks on this Linux host,
not predictions of Firefox startup, Docker latency or iPad throughput. RSS
counts kernel/CPU aliases repeatedly; it must not be interpreted as distinct
physical storage. PSS attributes their share once.

## Validation and boundaries

- Six new storage/coherence tests cover physical owner identity, coordinator plus
  four simultaneous CPU workers, CPU/native visibility without commit, mixed
  atomic/exclusive monitors, alias pair reads, same-registry fork COW and an
  externally retained writer. The private-file test verifies that its first COW
  write returns to the shared source path on the next epoch. The retained-slot
  test covers same-window live neighbors and a view from an older snapshot;
  mapping changes must preserve the retired physical bytes.
- A software zero-page test verifies no backing allocation on reads and one
  initialized allocation on first write, preserving untouched bytes.
- The VFS test checks ordinary mapped files and memfd: worker writes are visible
  to fd reads and read-only private aliases before metadata commit, native writes
  invalidate worker monitors, executable aliases change code tokens, and sync
  persists the final bytes. Existing truncate/unlink/protection/disk-reopen tests
  remain applicable.
- The core mapped-I/O service test now expects shared bytes before commit, while
  retaining its joined fallback for dirty metadata and persistence.
- Local affected checks: memory 78, core 81, Linux 27, mapped VFS 10; strict Clippy.
  Three Linux socket tests need the environment's network capability and passed
  with it. The original-musl ARM64 512MiB sparse-BSS probe passes with 4 CPUs and
  a 4GiB aggregate/per-process budget: `resident_kib=80`, COW/discard PASS.
- CI adds fully instrumented std/core/alloc/test ThreadSanitizer for the new shared
  storage, guard and file/memfd tests, plus Apple ARM64/16KiB affected modules and
  the unchanged original-musl fixture. Producer run `39381684188` supplies that
  fixture; CI first verifies its source/build script are unchanged. No IPA.

Writable private file pages that have not detached, and legacy custom backings
without the resident-storage API, retain their conservative shadow transport.
Warm software/native stores now reuse the paid TLB bit; initialized reads no
longer load the quota state, and quota membership uses relaxed loads rather than
another byte-publication acquire. These changes do not change guest fences or
the CPU/native region-borrow protocol. File persistence and mapping mutation
still need coordinated checkpoints. Those
boundaries do not serialize whole native functions or allocate one RAM copy per
core. No claim of complete weak ARM outcomes or lock-free byte transport is made;
see [region-memory.md](../architecture/region-memory.md) for the guard/unsafe proof.

Firefox first-frame acceptance remains open in issue #33. This isolated storage
comparison establishes the removed duplication; the earlier browser RSS figures
must not be retroactively interpreted as PSS or unique resident RAM.
