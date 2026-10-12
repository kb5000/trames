# ADR-007 — Opt-in NativeCall v1 pure-memory profile

Status: Accepted for the first accelerator implementation by owner direction,
2026-10-12. ADR-003's one-interpreter/no-JIT decision remains in force.

Start with a bounded public LP64 C memory/string ABI, an independent ordinary
Linux ELF shared library and original musl fallbacks. Do not replace an entire
libc/loader before its private state, TLS and allocator contract is implemented.
Reuse pinned upstream sources and preserve their licenses under GPL-3.0-or-later.

Reserve HLT #0x5452 only inside host-enabled NativeCall v1 operation. With the
feature disabled, this encoding remains ordinary SIGILL; all other HLT/BRK
behavior stays architectural. An enabled caller must match the exact signed-in
pure stub contract on every entry. Matching bytes/profile IDs do not grant
privileges: the only backend effects are validated bounded guest memory access.

Use private auxv key 0x54520001 for activation/discovery, value 0x10001 for
protocol 1/module 1. x0..x2 carry typed inputs, x16 profile, x17 function;
x0/x1 return a private two-word value/fallback aggregate. The guest wrapper
preserves the normal exported libc function ABI. A fault holds the HLT PC and
input registers; a successful native or fallback return commits and retires one
instruction. Never dispatch this protocol through Linux syscall numbering.

Use bounded temporary host slices for the first implementation. They keep COW,
shared file aliases, executable-cache invalidation, EOF SIGBUS and tags in the
existing Memory APIs. They are not authoritative guest allocator state. A
64 KiB work ceiling returns a typed fallback before write effects; the guest
executes the original implementation. No async borrows, callbacks, variadic
interfaces, device handles or downloaded host executable code in this profile.

Measure activation on/off using identical guest binaries. Record small-call
regressions as well as bulk gains. Follow-up zero-copy borrows or size-based guest
fallback policy need their own correctness/performance evidence; this decision
makes no physical-iPad or whole-application speed claim.
