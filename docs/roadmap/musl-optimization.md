# musl optimization queue

Owner direction, 2026-10-13 (Asia/Shanghai): operate directly on guest memory,
use native implementations in more cases, then extend worthwhile musl calls in
small verified batches. No JIT; preserve one cached Rust interpreter. The shipped
0.13.0/build 14 remains an immutable release. This is its development follow-up.

## Current batch

The full standard-path musl now adapts nineteen pure functions. IDs 1–5 remain
memcpy, memmove, memset, memcmp and strlen; IDs 6–10 add memchr, memrchr, strnlen,
strcmp and strncmp. IDs 11–17 add strchr, strrchr, strchrnul, strcpy, stpcpy, strncpy
and stpncpy. IDs 18–19 add strcat and strncat. [Integer parsing](../architecture/native-stdlib.md) adds six public
conversions and their existing internal aliases. Existing static binaries keep their embedded libc. The old
separate five-function library remains a transport test fixture.

Checked, scoped page borrows let native routines work directly on anonymous
guest bytes, including fork COW and CLONE_VM. Private file pages detach before
writes and can then be borrowed; their live EOF check remains active. Executable
writes advance the existing decode tokens. Guest virtual addresses are translated,
never cast to host pointers; references cannot escape a callback or be retained
across a scheduler turn. The page table remains sparse, with 4 KiB fragments.

Copy/move iterates in the correct direction for overlapping anonymous ranges.
Within one page, Rust's host memmove handles overlap; between pages, the pinned
original Arm memcpy assembly operates on disjoint slices. Copy involving a file
or shared backing takes one complete bounded source snapshot to preserve aliases,
then writes through the backing contract. Resident shared anonymous/file backing reads and writes now borrow scoped
fragments with dirty/version hooks; other backing implementations retain bounded staging. These paths preserve coherence rather than claim all mappings
are directly borrowable. Fork's first write may allocate a COW page.

memcmp, string scans and searches borrow reached readable fragments directly;
live backing reads use bounded snapshots. Host word scans skip eight bytes at a
time, then determine the exact unsigned-byte result. memset uses the original Arm
assembly on directly writable pages. Matching stubs still authorize only these
pure operations, not arbitrary native execution or stateful host libc calls.

The new auxv key `0x54520002` supplies a function bitmap, bit `(id - 1)`; current
value `0x1ffff`. Activation key/profile `0x54520001`/`0x10001` stay unchanged. New
wrappers require their bit; an old host with no bitmap uses original musl for new
functions. Early loader stages and ordinary Linux use original routines. Known
sizes below 128 bytes stay local; strcmp/strncmp and strnlen stop locally within
a 64-byte prefix. strnlen can terminate within the native ceiling even with a larger bound;
exhausting the ceiling returns a typed original-code fallback. No partial writes
occur on a ceiling fallback. Bulk memcpy/memmove/memset/memcmp/memchr/memrchr now accept up to 1 MiB.
String scans/copies/concatenation now accept 1 MiB per scan/input with original fallback
before effects. Calls remain synchronous joined checkpoints; this is a bounded
expansion, not a resumable unlimited operation.

Metrics add per-function entry counts, direct/buffered read/write bytes and retain
calls/fallbacks/faults/body time. Byte counts describe supplied fragments, not
exact physical loads or COW allocations. This makes hidden staging visible.

## Prioritized candidates

This is a queue of candidates worth measuring, not a claim that each host call
will be faster. Related public aliases should share implementations. Retain exact
original musl sources and notices for fallback; prefer its algorithms or Arm
assembly when translating native code. Avoid replacing musl with host Darwin libc.
[Current validation and measured limits](../validation/musl-optimization.md)
include both the bulk-copy gain and the sort regression/host timing variation.

| Priority | Calls or mechanism | Benefit and required work | State |
| --- | --- | --- | --- |
| P0 | memcpy, memmove, memset | Remove extra full-range allocations/copies; COW, alias and code-write correctness | Direct anonymous/private write path implemented; shared staging retained |
| P0 | memcmp, strlen | Remove page copies; native word equality/HASZERO scans | Implemented |
| P0 | memchr, memrchr, strnlen, strcmp, strncmp | Bulk text, parser and path workloads; bounded unsigned-byte results and early termination | Implemented, gated IDs 6–10 |
| P1 | strchr, strchrnul, strrchr | Reuse forward/reverse search with NUL termination and pointer results | Implemented, gated IDs 11–13 |
| P1 | strcpy, stpcpy, strncpy, stpncpy | Reuse checked scans/copies; distinguish terminator pointer, padding and bounded truncation | Implemented, gated IDs 14–17; preflight and original fallback before effects |
| P1 | strcat, strncat | Compose destination-end scan and bounded copy; preserve zero-bound semantics | Implemented, gated IDs 18–19; complete preflight and original fallback |
| P1 | memccpy, bcopy, bzero, explicit_bzero | Reuse copy/fill, exact stopping pointer and non-elidable zeroing contract | Queued; tiny aliases may remain local |
| P1 | Shared anonymous/file page borrowing | Remove remaining staging; pinned resident spans, EOF and backing revision hooks, alias-safe moves | Resident anonymous/file borrowing implemented; source snapshots retained for live aliases |
| P1 | Checked multi-page spans and native NEON scans | Reduce per-page dispatch; speed bulk compare/search/fill with bounded SIMD tails | NEON page-fragment search/compare implemented; multi-page spans queued because sparse mappings/COW are not universally contiguous |
| P1 | Operations beyond 64 KiB | Keep large copies/scans native while retaining scheduling checkpoints | Bulk operations up to 1 MiB implemented; larger work remains original fallback, future continuation must never replay partial moves |
| P1 | Call thresholds and prefix policy | Avoid spending more on validation than the guest operation saves | Current 128/64 policy retained; tune from occasional device/application measurements |
| P1 | Linux pathname/string reads | Use checked page borrows while constructing the necessary owned syscall argument; avoid copying/allocating an entire page for a short name | Implemented at the checked Memory/VFS boundary |
| P2 | strspn, strcspn, strpbrk | Character-set tables and native bulk scans; small sets can stay local | Queued; no shared global scratch |
| P2 | strstr, memmem | Long searches benefit from original musl's linear-time algorithms | Queued; cap scanned work and retain continuation/fallback semantics |
| P2 | strcasecmp, strncasecmp and `_l` forms | Text matching; unsigned-byte folding | Start with C locale; guest locale objects need a checked contract |
| P2 | strdup, strndup | Compose native scans/copies with guest allocator ownership | Defer until allocation handoff is explicit; never return a host malloc pointer |
| P2 | strtol/strtoul/strtoll/strtoull, strtoimax/strtoumax | Long numeric parsing using original algorithms | Implemented from unchanged original intscan; exact bits/end/errno and independent capability |
| P2 | strtod/strtof/strtold and related parsing | Often instruction-heavy; reuse original musl conversion code | [Native strtod/strtof, atof and locale aliases](../architecture/native-numeric.md) implemented; strtold stays original binary128, errno/end exact and non-nearest fallback |
| P2 | sin/cos/tan, exp/log/pow, sqrt/hypot, remainder/fmod, rounding and float variants | Native FP already accelerates interpreted arithmetic; library-level calls can remove control-flow cost | Guest FP state/errno and exceptional values need an explicit profile; avoid duplicating cheap one-instruction operations |
| P2 | Integer/float formatting within snprintf/sprintf | Accelerate costly conversion loops | Begin with pure internal conversion pieces; guest varargs and formatting must keep musl semantics |
| P2 | qsort/qsort_r, bsearch | Native sorting/search loops for large arrays | Implemented [guest-owned callback steps](../architecture/native-callbacks.md); qsort routes supported arrays, bsearch is explicit after entry-cost measurements |
| P2 | fnmatch, glob, regex compilation/matching | Large loops/tables may pay for native entry | fnmatch can start pure; regex-owned pointers/allocator and glob filesystem effects need exact contracts |
| P3 | malloc, calloc, realloc, free, aligned_alloc, posix_memalign | Large allocator bookkeeping could run natively | Guest heap pointers, musl metadata layout, locks, fork and mmap/brk authority; exact build/layout gating |
| P3 | fread/fwrite, fgets/fputs, getline/getdelim, printf family | Copy/format buffering and syscall batching | Guest FILE layout, errno/TLS, descriptor ownership and cancellation; no host FILE substitution |
| P3 | pthread mutex/cond/rwlock, semaphores, atomics/futex helpers | Reduce repeated interpreter/syscall glue | Guest scheduler/futex authority, cancellation and robust ownership remain mandatory |
| P3 | locale, iconv, multibyte/wide string operations | Conversion loops and larger text workloads | Guest locale/TLS, restartable conversion state and wchar ABI first |
| P3 | time/calendar conversion, getaddrinfo and other I/O glue | Some pure calendar loops qualify; networking already has native egress | Separate pure conversion from guest errno/TLS, DNS/files and scheduling |
| Keep local | ctype tests, abs/ffs/clz, simple byte swaps, small wrappers | Usually only a handful of instructions; transport can cost more than work | Optimize decoder/native compute if profiling shows a cost; no blanket NativeCall wrapper |

The [numeric measurements](../validation/native-numeric.md) completed the
sort-comparator follow-up: native floating parsing improves the unchanged
BusyBox sort-n workload in that measured batch. The subsequent
[integer/string batch](../validation/native-stdlib.md) completes the next pure
coverage group and includes actual Vim startup and Python imports/SQLite/hash
workflows. Function-level wins do not establish application-level gains.

The next priority is entry/return and checked-memory cost: retain exact executable
stub validation and invalidation, but investigate repeated checks, guest frame
transport and short-call routing. Then address coherent shared/multi-page spans.
Measure these costs before adding formatting/math or stateful allocator/stdio
contracts. No JIT or additional interpreter backend is planned.

## Implementation order and acceptance

1. Completed: this batch's ARM64/ordinary-musl comparisons, direct/staged metrics,
   Linux/Apple CI and actual simulator tests; [evidence](../validation/musl-optimization.md).
2. Completed: P1 string search/copy and native integer parsing, with zero sizes,
   unsigned bytes, guards, NUL/truncation, tags, errno, write-only/shared fallback
   and old-host capability negotiation. Reduce boundary and memory costs next.
3. Borrow coherent resident backing pages where the storage abstraction supports
   it; measure bulk data movement and one unchanged real-program workload.
4. Add native SIMD or contiguous-span paths only when measurements identify a
   remaining bottleneck. Preserve permission/EOF preflight and code tokens.
5. Select P2 work from actual application profiles. Stateful P3 needs its own
   versioned layout/ownership contract and should not hold up useful pure calls.

For each implemented group, check off/on and ordinary Linux, fork/shared VM,
file aliases/truncation, signal recovery and self-modifying executable pages.
Keep original fallback selectable. Use occasional same-binary before/after
benchmarks, state the host and workload, report regressions, and avoid claiming
QEMU results are physical-iPad or whole-system speedups. Bundle device releases
at a meaningful milestone because of the owner's signing limits.

The [entry/memory follow-up](../architecture/native-boundary-cost.md) adds
version-checked stub reuse, direct resident pathname reads and single-page write
validation. Application benefit is determined by the recorded before/after batch.
Owner policy permits audited unsafe Rust boundaries with explicit operations,
rather than a blanket production ban.

The owner prioritized [multicore execution](../architecture/multicore.md) before
further memory/native work. Host workers now execute guest CPU slices concurrently;
NativeCall, syscalls and backing hooks initially retain joined checkpoints. Ordinary
guest data races can interleave, while host mutable accesses remain race-free and
ARM atomic/fence constraints remain enforced. Subsequent borrowing/SIMD work should
reduce these measured boundaries without transporting mutable Rc/VFS objects or
unvalidated host pointers to CPU workers. [Measurements](../validation/multicore.md)
include both multicore scaling and single-core overhead.

The [continuous-worker follow-up](../validation/continuous-workers.md) removes
per-chunk dispatch/return and startup barriers. Workers retain CPU/view state across
local interrupt polls; kernel/native/mapping events still require a joined checkpoint.
Its measured limits are kept separately from the released 0.12 multicore batch.

The [coherent backing/bulk follow-up](../validation/bulk-native.md) records 19 pure
entries, scoped resident backing access, bounded 1 MiB memory operations and NEON
fragment scans. Timings retain short-copy regression and small real-program change;
no universal application speed or physical-iPad claim.

The reduced-fallback follow-up extends pure string scans to 1 MiB and permits
resident shared/write-only one-shot outputs (including numeric result frames).
Callback state keeps its stricter private/readable gate. Huge forward scan/compare
bounds can stop natively before exhausting the ceiling. `function_fallbacks` makes
remaining pure fallbacks visible; the expanded stdlib fixture has zero such
fallbacks, six long integer-input fallbacks, versus 169 total fallbacks on the
saved 17-function host using the same new libc/fixture. This is fixture evidence,
not a claim that every application or arbitrary input avoids fallback.

The [worker-side NativeCall implementation](../architecture/parallel-nativecalls.md),
tracked in [#16](https://github.com/kb5000/trames/issues/16), removes the global stop
per admitted native call through one Memory/View transport. The 19 pure entries,
integer/floating parsers and guest-owned sorting/search steps continue on their
owning CPU. Live private file backing defers before writes; syscalls and persistence
remain joined. [Validation and measurements](../validation/parallel-nativecalls.md)
record the supported concurrency boundary and residual bandwidth/checkpoint costs.
