# musl optimization queue

Owner direction, 2026-10-13 (Asia/Shanghai): operate directly on guest memory,
use native implementations in more cases, then extend worthwhile musl calls in
small verified batches. No JIT; preserve one cached Rust interpreter. The shipped
0.10.0/build 11 remains an immutable release. This is its development follow-up.

## Current batch

The full standard-path musl now adapts seventeen pure functions. IDs 1–5 remain
memcpy, memmove, memset, memcmp and strlen; IDs 6–10 add memchr, memrchr, strnlen,
strcmp and strncmp. IDs 11–17 add strchr, strrchr, strchrnul, strcpy, stpcpy, strncpy
and stpncpy. [Integer parsing](../architecture/native-stdlib.md) adds six public
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
then writes through the backing contract. Shared backing reads/writes still stage
bounded fragments. These paths preserve coherence rather than claim all mappings
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
occur on a ceiling fallback. The ceiling remains 64 KiB per native operation.

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
| P1 | strcat, strncat | Compose destination-end scan and bounded copy; preserve zero-bound semantics | Queued after string copy |
| P1 | memccpy, bcopy, bzero, explicit_bzero | Reuse copy/fill, exact stopping pointer and non-elidable zeroing contract | Queued; tiny aliases may remain local |
| P1 | Shared anonymous/file page borrowing | Remove remaining staging; pinned resident spans, EOF and backing revision hooks, alias-safe moves | Queued; preserve a snapshot when needed |
| P1 | Checked multi-page spans and native NEON scans | Reduce per-page dispatch; speed bulk compare/search/fill with bounded SIMD tails | Queued; sparse mappings and COW prohibit a universal contiguous pointer |
| P1 | Operations beyond 64 KiB | Keep large copies/scans native while retaining scheduling checkpoints | Currently original fallback; cooperative chunks need overlap direction, alias coherence and a continuation that never restarts partially moved data |
| P1 | Call thresholds and prefix policy | Avoid spending more on validation than the guest operation saves | Current 128/64 policy retained; tune from occasional device/application measurements |
| P1 | Linux pathname/string reads | Use checked page borrows while constructing the necessary owned syscall argument; avoid copying/allocating an entire page for a short name | Queued for the generic Memory/VFS boundary |
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

The [callback measurements](../validation/native-callbacks.md) make numeric
parsing a concrete follow-up: native qsort removes about 54% of guest instructions
in the integer fixture, but only about 2.5% in unchanged BusyBox `sort -n`.
Its comparator still calls guest atof/strtod. Evaluate that path next, alongside
boundary cost, rather than treating reduced continuation bytes as a speed result.
Guest callbacks can now nest another pure native conversion; end pointers,
errno/TLS, locale and the ARM64 long-double ABI still need explicit contracts.

## Implementation order and acceptance

1. Completed: this batch's ARM64/ordinary-musl comparisons, direct/staged metrics,
   Linux/Apple CI and actual simulator tests; [evidence](../validation/musl-optimization.md).
2. Add P1 string search/copy functions in small groups. Verify zero sizes, unsigned
   characters, guards, NUL/truncation, tags, errno preservation and fallback.
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
