# Node REPL regression investigation

The owner reports `local runtime panicked` after entering text such as `111` in
Node. This is a caught host Rust panic, rather than a Node exception. The owner clarified that it happens before pressing Enter in the latest
build 34; the eager-preview path is therefore part of the reproduction.

`tests/integration/node-repl.py` exercises unmodified Alpine Node 22.23.2 through
the production App FFI, hybrid musl, Wayland and multiple-terminal settings. It
uses 4 GiB aggregate/per-process memory and disabled networking. Both terminal 1
and a newly opened terminal 2 evaluate whole-line `111`, delayed character input
with a one-second eager-preview interval, arithmetic and an object. It requires
actual result lines, subsequent REPL prompts, `.exit` and a complete shell output
marker; input echo alone cannot pass. Logs and current status survive failure.

The Linux x86_64/4 KiB host passed all eight checks and returned to both shells
using the unchanged runtime from `95830f6`. This does not establish iPad acceptance
or close the regression. [Results](node-repl/linux-results.json) and
[checks](node-repl/linux-checks.log) retain the runtime/libc hashes.

The manual `Node REPL compatibility` workflow builds the current hybrid musl
from pinned original sources, then tests the same production FFI on Apple ARM64
with an asserted 16 KiB host page size. It deliberately avoids rerunning unchanged
CPU, VFS, Linux, network, graphics and UI suites, and creates no IPA.

## Confirmed 16 KiB failure and correction

The original runtime also fails on actual Apple ARM64/16 KiB pages:
[run 39432821824](https://github.com/kb5000/trames/actions/runs/39432821824),
source `97aa8adbef13fb9a52752553eb27df2499123a52`.
[The check annotation](node-repl/apple-before-annotations.json) records
`crates/trames-memory/src/parallel.rs:1226: mapping changes require joined commit`
while typing delayed arithmetic, before Enter. The preceding two `111` cases
passed; this is not a claim that every first numeric input fails deterministically.
A Linux diagnostic that changes only MMU granularity from 4 KiB to 16 KiB
reproduces the same panic. It is not an Apple architecture oracle.

A coordinator MMU rebuild can copy an entire host group into a new physical
window. On a 16 KiB host, changing one 4 KiB guest page can therefore replace
three otherwise unchanged neighbor pages. The incremental parallel snapshot
previously refreshed only the requested mapping, retaining old SharedPage
identities for those neighbors. The next dirty worker commit detects the mismatch
and panics; it could also read/write stale storage before that assertion.

The fix records **each physically relocated page** in the existing parallel
mapping-change set, while the coordinator already owns the quiescent mapping
boundary. It preserves the assertion, guest permissions, byte guards and shared
ordinary-access fast path. It adds no per-instruction checks, locks or global
serialization.

The new deterministic memory regression retains a worker view, rebuilds one
4 KiB mapping in a 16 KiB group, releases the old view to enable the incremental
snapshot path, then writes and verifies every neighbor through the next epoch.
It [fails before](node-repl/regression-before.log) and
[passes after](node-repl/regression-after.log) the fix. Linux passes all
90 affected memory tests and strict memory Clippy. The 16 KiB diagnostic now
passes all eight original Node REPL checks in both terminals, including delayed
eager preview. [Results](node-repl/linux-host16-after.json).
The actual Apple ARM64 check passes on fixed source
`a297404f01821e453b29fbc15a483c7b5773c1b0`:
[run 39434031673](https://github.com/kb5000/trames/actions/runs/39434031673).
All 90 memory tests, strict memory Clippy and the eight two-terminal original
Node REPL checks pass with the asserted native 16 KiB page size. The unchanged
hybrid musl artifact is reused after checking its complete guest source closure.
[API evidence](node-repl/apple-after-ci.json) retains exact job/step outcomes and
artifact digests. The artifact was not downloaded locally because the configured
proxy denies the Actions binary-storage endpoint. No new iPad or IPA acceptance
is claimed.

Owner report tracked and code fix completed in [Issue #34](https://github.com/kb5000/trames/issues/34).
The subsequent private-file atomic COW source also passes the Apple REPL checks:
[run 39437406520](https://github.com/kb5000/trames/actions/runs/39437406520).
This still does not install or validate a new package on the owner's iPad.
