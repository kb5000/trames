# Node.js compatibility and interpreter memory

This source follow-up keeps the single cached Rust interpreter and existing
NativeCall profiles. It introduces no host JIT or executable-memory allocation.
Node's ordinary V8 can generate **guest AArch64 instructions**; Trames fetches,
decodes and interprets them with the same permissions, code revisions and budget
rules as other guest code. The compatibility fixture uses no `--jitless` flag.

## Private anonymous reservations

An anonymous private `mmap(PROT_NONE)` records an ordered, disjoint virtual range
without allocating a frame per page. The guest's 48-bit address limit still
applies. At most 65,536 reservation intervals are retained, including fragments
created by partial operations. Metadata exhaustion returns ENOMEM before effects;
reserved-range remapping is conservative near that cap.

`mprotect` validates the whole range and backing write rights first. Giving a
reserved range access commits zero-filled page objects under configurable
per-space and scheduler aggregate budgets (defaults: 512 MiB and 1 GiB).
A failed commit preserves bytes,
permissions and reservation ownership. Making an already committed page
inaccessible retains its page object; this is not a general demand-paging or
host-RSS model. Shared anonymous and file-backed mappings keep their existing
coherence, write-right, EOF and COW contracts.

Free-range searches include reservations without walking their individual pages.
MAP_FIXED, partial munmap and commits split ranges; adjacent new reservations can
merge. Fork copies ownership while preserving page COW; CLONE_VM shares both.
Entirely uncommitted ranges support selected mremap operations. Mixed committed
and reserved remaps retain the existing single-mapping restriction.

Access to a reservation faults with permission denial, while a removed hole is
unmapped. mincore returns zero for uncommitted pages; its output range remains
bounded by the independent accessible-page metadata ceiling, rather than the guest RAM budget. msync and accepted advice hints validate reservation ownership
without allocating a residency vector; DONTNEED preserves uncommitted ranges.
`/proc/PID/maps`, VmSize, statm and stat distinguish virtual ownership from
committed page accounting. VmRSS describes the emulator's committed pages, not
physical host residency or shared-page proportions.

## Process and socket ABI

The initial stack packs argv strings in increasing address order, followed by env
strings in increasing order. Pointer arrays retain their original argument order
and the initial SP remains aligned. libuv's process-title buffer calculation can
then clear this contiguous area without underflowing its size.

rt_sigaction accepts musl's sign-extended representation of SA_RESETHAND by using
the low 32 flag bits. The selected low-bit flag validation and signal-delivery
rules remain. Standard streams accept O_NONBLOCK through fcntl/FIONBIO, share it
through dup and preserve per-descriptor CLOEXEC. The embedding adapter's
WouldBlock becomes EAGAIN; the scheduler retries only blocking descriptors.

TCP_KEEPIDLE, TCP_KEEPINTVL and TCP_KEEPCNT validate Linux ranges and support
readback. Individual TCP settings preserve SO_KEEPALIVE. Native sockets translate
to socket2's host API, including Apple's TCP_KEEPALIVE equivalent. Virtual TCP
uses KEEP IDLE as smoltcp's idle/probe period; interval/count are retained
compatibility hints. They do not implement Linux's separate probe interval,
retry-count expiration or host-gateway TCP tuning.

## Interpreter hot path

Small translation, permission/backing and resident-copy helpers expose their
existing implementations for compiler inlining. Fixed-size operand copies can
then use host loads/stores rather than an opaque variable-size copy call. Frame
lookup, TBI, permissions, cross-page preflight, file checks, COW and executable
write invalidation remain enforced. This change adds no raw pointer cache or
unsafe memory access.

The signed Alpine integration test installs Node.js and npm, exercises default
V8 execution, title updates, asynchronous filesystem/crypto operations, workers,
child processes, HTTP client/server and fetch, then performs an offline local
dependency install and reopens persisted data. The manual benchmark compares
unchanged inputs and fresh copies of Vim/Python stores with a saved baseline.
See [validation](../validation/nodejs-memory.md) for measured results and limits.
