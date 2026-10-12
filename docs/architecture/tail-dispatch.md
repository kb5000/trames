# Explicit tail-call research

The 0.7 investigation keeps the production interpreter on stable Rust 1.90.0.
Rust's `become` syntax requires the incomplete `explicit_tail_calls` feature;
the pinned compiler rejects it on the stable channel (E0554). Ordinary recursive
Rust calls are not a guarantee of bounded stack usage. Neither nightly nor
`RUSTC_BOOTSTRAP` is enabled for production.

Clang 17 accepts `[[clang::musttail]]` under both the Linux x86_64 and iOS arm64
ABIs. [The freestanding prototype](../../tests/performance/dispatch.cpp) gives
each handler exactly the same signature and uses compiler-enforced tail calls
between handlers. It builds without Apple SDK headers for arm64 iOS 17.
Ten million steps at `-O0` complete with a 256 KiB stack and match the ordinary
loop's checksum and final PC. This verifies stack boundedness for the prototype,
not compatibility of a complete Trames backend.

[The harness](../../tests/performance/dispatch.py) compares switch, computed-goto
and musttail dispatch on the same predecoded synthetic ADD/mix/branch stream.
All three execute 100 million operations per sample and finish with the same
checksum and state. Three interleaved warm-host samples with Clang 17 gave median
times of 0.107 s for switch, 0.191 s for computed goto and 0.178 s for musttail.
The [raw report](../validation/nonjit-0.7-tail.json) retains samples and scope.
These numbers exclude guest memory, exception semantics, syscalls and Rust/C++
state transfers. They are neither real-program nor physical iPad speedups.
They establish that guaranteed tail calls alone do not ensure faster dispatch.

The default backend therefore retains Rust iteration. The shipped changes reduce
invalidations and combine warmed arithmetic/conditional-branch dispatch, with
the single-step reference implementation as oracle. Adopting tail dispatch would
require either stable Rust support or a full static C++ decoded executor,
including checked memory and all fallbacks without a per-instruction FFI bounce.
Its fault states, budgets, cancellation and code-store invalidation must pass
the same tests, then show a benefit on actual programs and iPad hardware.

Reproduce the isolated experiment on a POSIX host with Clang:

```sh
python3 tests/performance/dispatch.py --clang clang++ --output /tmp/dispatch.json
```

It emits a timing report and arm64 iOS assembly. It creates no executable memory
at runtime and is not selectable as a runtime backend.
