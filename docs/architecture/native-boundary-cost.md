# Native entry and checked-memory cost

Development follow-up to the integer/string batch. NativeCall profiles, function
IDs, guest musl binaries and public ABI stay unchanged; the one cached interpreter
continues without JIT. The previous application measurements showed CPU
interpretation dominates startup. These changes target bounded repeated work,
with no assumed whole-system gain.

## Executable entry verification

The NativeCall registry has 64 fixed cache slots. Each entry records PC, module,
function and the existing memory identity/instruction-revision/backing-version
tokens for every page reached by the exact five-word stub. The whitelist remains
checked on every invocation. Execute permission, address/alignment/range and live
backing EOF checks run on every invocation through instruction_page_identity.

A token hit reuses the exact prior word comparison. A miss fetches and compares
all five words, then rereads the tokens before caching a coherent observation.
A stub crossing a page boundary requires both page tokens. No slice, host pointer,
page allocation or guest-owned handle is cached. The fixed cache is a derived
validation result, rather than authority to execute arbitrary host code.

Fork has a new memory identity. Mapping/protection changes and executable writes
change the affected page revision, including writes through native copy/fill.
Shared executable file aliases follow backing versions, and backing EOF is checked
even on hits. CLONE_VM delegates to the shared space identity. Unrelated mappings
and data writes can retain the checked entry. This uses the existing decoder's
invalidation contract; live backing implementations must publish mutations through
their existing version/revision hooks.

Metrics add validation_hits and validation_misses. A miss counter describes a
word-comparison attempt after valid page tokens, not every rejected module or
permission failure. Counts/body timing remain otherwise compatible. Scheduling,
retirement, cooperative native-return limits and guest callback behavior retain
their existing rules.

## Scoped reads and writes

read_c_string now borrows reached resident page fragments directly and appends
only the bytes before NUL to its returned owned vector. It removes a temporary
heap allocation and full-fragment copy for each page. Shared/live file pages use
the existing bounded stack snapshot and EOF checks. Bounds include the terminating
NUL; non-UTF-8 bytes and TBI pointer handling retain the existing semantics.
A reached terminator does not probe the following page. No borrow escapes.

with_write_chunk validates the single reached frame, write permission and live
backing once, rather than validating a range and looking up the same frame again.
The common page-contained fill uses that checked operation directly. Cross-page
fills still preflight the entire range before effects, then keep per-fragment
backing checks. Private file detachment, fork COW, shared write-through and
executable-code revision hooks remain. A zero-length fill invokes no operation.

## Controlled unsafe boundaries

Owner direction permits unsafe Rust in production at audited boundaries. The
workspace now denies unsafe_op_in_unsafe_fn instead of globally forbidding unsafe
code. Each unsafe operation needs an explicit block and a documented validity,
alignment, aliasing, lifetime, permission and thread-ownership contract. Prefer
interfaces whose caller receives checked values/scoped access. Guest virtual
addresses alone never establish host pointer validity. Current boundary guidance
is recorded in [dependency policy](../security/dependencies.md).

The optional tests/performance/boundary-cost package is an isolated measurement
process. Its allocation-counting GlobalAlloc forwards exact layouts and pointers
to System, with explicit unsafe blocks. Its locked dependencies and source are
identical when built against the preceding and current implementation; it is not
an application dependency or another CPU backend.

[Validation, allocation counts and measured application limits](../validation/native-boundary-cost.md)
record the actual outcome.
