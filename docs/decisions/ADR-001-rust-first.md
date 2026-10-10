# ADR-001 — Rust-first hybrid architecture

Status: Accepted in Engineering Charter v0.2, 2026-10-10.

Rust owns ELF loading, guest memory, Linux ABI, VFS, processes, distribution
management, Bridge RPC and accelerator registration. Swift/SwiftUI owns iPadOS
UI and system integrations. C/C++ may provide proven libraries and measured hot
paths. There is no language quota.

M0 implements the loader, memory, reference CPU, syscall dispatch and CLI in Rust
with no external crate dependencies. Future subsystem crates are added with
working behavior when their milestones require them, rather than empty APIs.

Every future cross-language boundary uses versioned C ABI, opaque handles,
explicit ownership and errors. Rust layouts, trait objects, C++ classes and
exceptions do not cross that boundary. Calls are batched per block or time slice.

Consequences: Linux remains the primary developer platform. Apple build tooling,
ordinary developer signing and actual iPad validation are separate required work
before claiming an iPadOS product.
