# ADR-002 — Reference Rust and optional C++ interpreters

Status: Accepted in Engineering Charter v0.2, 2026-10-10.

The Rust reference interpreter prioritizes correctness and diagnostics. An
optional C++ backend may later use computed goto, validated predecoded blocks
and verified instruction fusion, with compiler feature checks and a portable
fallback. Both must obey the same architectural and memory semantics.

M0 delivers the Rust backend with bounded slice outcomes and independent QEMU
checks. M5 implements the optional C++ integer-block backend in `native/interpreter-cpp`,
with Rust fallback and independent QEMU checks; see [M5](../architecture/m5.md).
The Rust backend remains the default after workload-dependent profiling.

The C++ backend will accept validated blocks, guard every opcode index and block
bound, validate guest memory accesses, and return syscall/fault/yield/exit events.
No guest address directly produces a host pointer. Side effects are never
blindly retried after partial execution. No dynamic executable code is generated.

Optimization requires measured workloads and differential correctness tests
against Rust and QEMU. Device, flags, workload and peak RSS accompany published
performance results.
