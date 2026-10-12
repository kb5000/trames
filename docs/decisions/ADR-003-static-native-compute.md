# ADR-003 — One static interpreter with native compute

Status: Accepted by explicit owner direction, 2026-10-12.
Supersedes the execution-backend choice in ADR-002 and Engineering Charter v0.2.

Keep one bounded Rust cached interpreter. Remove the optional C++ executor and
the unsuccessful tail-dispatch prototype, their runtime selectors and build
features. Retain single-step instruction semantics and the uncached loop only
as test/probe machinery. No JIT or dynamically generated executable code.

Use native ARM64 floating-point and SIMD/crypto arithmetic first. A value-only
compute crate isolates static inline assembly and FPCR/FPSR scope management;
guest address translation and data permissions remain in safe Rust. Optional
instructions are checked against host features and retain functional fallbacks.
The owner accepts host/fallback differences in rounding, NaNs and exception flags
in exchange for native arithmetic speed. The x86_64 implementation serves the
development/CI host only; both Linux guests and shipping iPads are ARM64.

Prioritize functional instruction coverage. PAC/BTI and MTE checks may be disabled
in the documented compatibility profile; real branch, load, address, writeback
and zeroing effects must execute. Do not advertise those protection mechanisms
as complete Linux hardware capabilities. Unsupported SVE/SME remain SIGILL.

Compare actual ARM helper execution with the pinned independent QEMU oracle,
test faults and recovery with real musl binaries, and preserve representative
decode audits separately from semantic evidence. Publish performance results
only for measured workloads, with host/device limits stated.
