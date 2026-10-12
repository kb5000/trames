# Static interpreter acceleration (0.6.0 / build 7)

The default local backend is now `cached`: a Rust interpreter with bounded,
data-only lazily decoded AArch64 code pages. `reference` keeps the single-step decoder
as an architectural oracle, and `cpp` remains the optional static integer-block
interpreter. All three run the same CPU feature profile and virtual Linux ABI.
None allocates executable memory, emits native code, or needs a JIT entitlement.

## Decoded execution

Each CPU owns 128 direct-mapped code-page slots, each containing at most
1,024 generation-tagged decoded instructions. The full virtual page number is hashed;
replacing one slot advances its generation without allocating or clearing the
backing array. Generation wrap explicitly clears tags before reuse. Only reached PCs are fetched
and decoded, so cold or inaccessible instructions are never speculatively read.
Execution stays in the selected page across branches and reference fallbacks,
looking up a page again only at a page transition, scheduler/syscall boundary,
or code-epoch change. This removes per-basic-block lookup and repeated prefix
allocation. Fork copies architectural state and starts with an empty cache.

Common arithmetic, logical masks, bitfields, conditionals, shifts, multiply/divide,
and scalar/vector loads and stores are decoded once. Immediates, scaled offsets,
register roles and reserved-field validation are cached. Other instructions use
the complete reference semantics without copying CPU state or crossing an FFI
boundary. Floating-point/SIMD exceptions, atomics and syscalls retain their
existing behavior; the cache does not advertise additional CPU capabilities.

A code page is keyed by address-space identity, virtual base and code epoch.
Independent spaces cannot reuse code merely because their revision counters
match. Mapping/protection changes and successful stores to executable pages
invalidate decoded entries. A possible code store checks the epoch before the
next cached instruction, including self-modification inside a page. CLONE_VM
threads observe the same identity, epoch, mappings and bytes. Ordinary data
stores preserve decoded pages.

Execution reports a fault only at the PC actually reached, checks branch-target
alignment even within a warmed page, preserves the single-step fault state, stops at
SVC, and respects exact instruction budgets. The 4,096-instruction scheduler
quantum, signal boundaries, cancellation, PTY behavior and virtual clocks remain
in force.

## Guest memory

An ordered sparse map retains mmap/range/procfs behavior. A reusable frame slab
and a 256-entry software translation cache make repeated page lookup constant
time on a hit. Cache entries contain only a page tag and a stable frame index:
they own no page bytes, expose no native guest pointers, and add no Arc reference
that could cause false COW. Unmapped frames release their bytes and are reused;
metadata is bounded by peak mapped pages rather than mapping-operation count.

Data permissions are checked on every access; execute rights are preserved by
the code-cache identity/epoch checks. Mapping/protection changes invalidate
translations in the shared space, and fork gets an independent cache. Fixed-size
1/2/4/8/16/32-byte operands avoid variable-size copies. Cross-page reads and writes
still validate the whole range before modifying a destination, guest registers,
writeback base, memory, or exclusive/code revisions. C-string reads scan a checked
page segment at a time and retain their original limits and byte semantics.

## Runtime overhead

Virtual mounts are initialized once, so `/dev` and `/sys` work even when a process
never reads `/proc`. Subsequent complete process snapshots occur only when an
operation traverses procfs or reads an open proc descriptor. The path probe uses
the real resolver, including cwd, dirfd, symbolic-link aliases, and failed lookups
of newly created process directories. Reads of proc files still refresh live
process, mapping, descriptor, signal and CPU accounting information.

Disk durability, manifest format, quotas and host-handle policy are preserved.
This release does not trade fsync/recovery correctness for package-install speed.
The M5 descriptor-exhaustion fix remains in place.

## Measurement and limits

`--metrics` reports total instructions/time, CPU slice time, syscall time, idle
time, syscall count, proc refresh count, scheduler slices, decoded-page lookups/misses and instruction decode
counts. Timings include host scheduling and are not hardware cycle counts.
Syscall timing is opt-in; counts and existing CPU accounting are bounded
scalars. CPU time includes cache preparation and instruction decoding. Total runtime time includes final
VFS sync; ELF/store loading precedes the runtime timer. The manual program
benchmark additionally measures process spawn to first visible Vim cursor, or
spawn to successful Python/compiler exit, so loading is included in those times.

Run [the comparison harness](../../tests/performance/nonjit.py) only for material
changes, using a preserved baseline binary and identical signed-package stores.
It alternates baseline reference and candidate cached order (optionally including
the candidate reference backend for attribution),
restores a fresh copy before every run, retains Vim's defaults/plugins/history,
and validates Python and GCC results. Timing has no CI pass/fail threshold.

For a physical iPad, Vim itself can record startup stages with
`vim --startuptime /tmp/vim-startup.log`; after quitting, inspect that file.
Host Linux throughput and simulator acceptance do not establish device speed.
The present results are a measured optimization series, not a proof of the
maximum possible non-JIT throughput. Further work should follow measured cache
misses, dispatch/operand costs and workload-specific syscalls; superinstructions,
additional static decoding and storage indexing remain possible.
