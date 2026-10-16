# Explicit tail-call interpreter

## Default interpreter, 2026-10-16

The owner has approved the full `tailhot` implementation for production. The only
cached interpreter now uses `become` and `extern "rust-preserve-none"`, pinned to
nightly-2026-10-16 in the toolchain file and both CI workflows. No backend switch,
JIT, executable allocation or RUSTC_BOOTSTRAP is needed. The one-step reference
semantics remain the correctness oracle and fallback implementation.

All handlers and cold decode transitions have the same signature, so explicit
tail calls guarantee bounded stack usage. A handler may call ordinary Rust
memory/native helpers, returning before its next tail transition. `TailState`
borrows CPU, memory, local lazy flags and the current code page only for one
bounded slice. Flags are published on every exit and around reference fallback.
The exhaustive Op dispatch selects a constant TAG for that same immutable entry;
there is no intervening cache mutation. Private five-bit register constructors
and masked code-page slots justify the internal unchecked bounds. Public
register indexing and guest faults retain their checked contracts.

The feature is incomplete upstream, so only this CPU crate acknowledges
`incomplete_features`; compiler upgrades must repeat workload and architecture
validation. The new compiler renames atomic `fetch_update` to `try_update` with
unchanged orderings and retry closures. CI retains all correctness checks; a few
advisory source-style lints are explicitly scoped/allowed during migration.

[Promotion, memory measurements and validation](../validation/tail-tlb.md).

## Original full Rust experiment

[The new investigation](../validation/dispatch-experiments.md) implements real
`become` transitions for all 26 cached instruction classes, with checked memory,
complete reference fallbacks, precise exits and code-store invalidation. It uses
nightly-2026-10-16 and compares the original loop on that same compiler. Rust,
`tail` and `rust-preserve-none` calling conventions were tested separately.
Tail-jumping into cold decode paths finally removes the stack frame from ordinary
ARM64 handlers; memory and fallback handlers still make ordinary calls.

The final prototype's three alternating host samples show CoreMark 1.153x,
memory 1.163x and GCC 1.049x, with Vim 0.988x and Python 0.965x. These are shared
x86_64 development-host observations, not physical iPad results. The historical experiment kept stable Rust; the subsequent promotion above
changes the default. Complete patches, compiler
fingerprints, rejected variants, correctness checks and timings are archived in
the new report; no extra selectable production backend is maintained.

## Historical 0.7 C++ microbenchmark

The older experiment files were removed from the active build after they showed
no benefit on their synthetic workload. That result is not evidence against the
new full Rust implementation. The linked 0.7 sources remain available as history.

The 0.7 investigation keeps the production interpreter on stable Rust 1.90.0.
Rust's `become` syntax requires the incomplete `explicit_tail_calls` feature;
the pinned compiler rejects it on the stable channel (E0554). Ordinary recursive
Rust calls are not a guarantee of bounded stack usage. Neither nightly nor
`RUSTC_BOOTSTRAP` is enabled for production.

Clang 17 accepts `[[clang::musttail]]` under both the Linux x86_64 and iOS arm64
ABIs. [The freestanding prototype](https://github.com/kb5000/trames/blob/35d83cd670f08c2a3f0152666930dab67f4c8f43/tests/performance/dispatch.cpp) gives
each handler exactly the same signature and uses compiler-enforced tail calls
between handlers. It builds without Apple SDK headers for arm64 iOS 17.
Ten million steps at `-O0` complete with a 256 KiB stack and match the ordinary
loop's checksum and final PC. This verifies stack boundedness for the prototype,
not compatibility of a complete Trames backend.

[The harness](https://github.com/kb5000/trames/blob/35d83cd670f08c2a3f0152666930dab67f4c8f43/tests/performance/dispatch.py) compares switch, computed-goto
and musttail dispatch on the same predecoded synthetic ADD/mix/branch stream.
All three execute 100 million operations per sample and finish with the same
checksum and state. Three interleaved warm-host samples with Clang 17 gave median
times of 0.107 s for switch, 0.191 s for computed goto and 0.178 s for musttail.
The [raw report](../validation/nonjit-0.7-tail.json) retains samples and scope.
These numbers exclude guest memory, exception semantics, syscalls and Rust/C++
state transfers. They are neither real-program nor physical iPad speedups.
They establish that guaranteed tail calls alone do not ensure faster dispatch.

The default backend at that stage retained Rust iteration. The shipped changes reduced
invalidations and combined warmed arithmetic/conditional-branch dispatch, with
the single-step reference implementation as oracle. The full nightly Rust
executor above now provides another concrete implementation path; C++ or stable
support are not prerequisites for experimentation. The current default follows that complete implementation; physical iPad speed
measurement remains separate from fault, budget, cancellation and code-store validation.

To reproduce the archived experiment, check out commit `35d83cd670f08c2a3f0152666930dab67f4c8f43` on a POSIX host with Clang:

```sh
python3 tests/performance/dispatch.py --clang clang++ --output /tmp/dispatch.json
```

It emits a timing report and arm64 iOS assembly. It creates no executable memory
at runtime and is not selectable as a runtime backend.
