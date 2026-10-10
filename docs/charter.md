# Trames — Engineering Charter

Version 0.2  •  Rust-first hybrid architecture  •  10 October 2026

## 1. Mission

Trames is a terminal-first iPadOS development environment combining native SSH and an unmodified AArch64 Linux userspace. It targets ordinary developer signing, without jailbreak, JIT entitlements, private APIs or hardware virtualization. It is a userspace compatibility runtime, not a Linux kernel or full-system VM. Product principle: Native where it matters. Compatible by default. Extensible by design.

## 2. Accepted architectural decisions

ADR-001 — Rust-first: Rust owns the ELF loader, guest memory model, Linux ABI, VFS, process scheduler, distribution manager, bridge RPC and accelerator registry. Swift/SwiftUI owns native iPadOS UI and system integrations. C/C++ is used for proven libraries and measured hot paths. No language quota.

ADR-002 — Dual execution backend: a reference Rust interpreter prioritizes correctness and diagnostics; an optional Clang/GCC C++ threaded interpreter uses computed goto, predecoded blocks and carefully verified instruction fusion. Both obey the same guest semantics and tests. No dynamically generated executable code.

ADR-003 — Versioned C ABI: Rust/Swift and Rust/C++ communicate via explicit opaque handles, POD structures, status codes and documented ownership. Never expose Rust trait objects, C++ classes, exceptions, or unstable layouts across the ABI. Batch interpreter calls per block/time slice, not per instruction.

ADR-004 — Linux ABI first: Alpine AArch64/musl/apk is the first validated distribution; Debian arm64/glibc/apt is a later target. No Alpine-specific behavior in the kernel compatibility layer.

## 3. Product experience

Termux-style terminal-first UX: launch directly into the distribution-configured default shell (e.g. Alpine ash), configurable extra keys, Unicode/VT terminal, true color, touch selection, hardware keyboard, tabs and optional splits. Native SSH/SFTP sessions and local Linux sessions share a transport-neutral terminal renderer but use separate implementations. Support ~/.ssh/config as the canonical host configuration source, with explicit warnings for unsupported native directives; host key verification is mandatory.

## 4. Runtime subsystems

trames-elf: validate ELF64 AArch64 headers, load PT_LOAD, construct argv/envp/auxv and later PT_INTERP.

trames-cpu: architectural state, instruction semantics, decoding, exception/SVC events, interpreter reference backend.

trames-memory: guest address spaces, page protections, mmap/brk, checked guest pointers and later copy-on-write.

trames-linux: syscall dispatcher, virtual process lifecycle, signals, futex, threads, sockets, pipes, PTYs and Linux error mapping.

trames-vfs: mount table, rootfs, proc/dev emulation and scoped host directory adapters.

trames-terminal: session abstraction and byte-stream I/O.

trames-distro: rootfs acquisition, verification, storage, backup and upgrades.

## 5. C++ execution backend

Place the optional C++ engine under native/interpreter-cpp. Use Clang/GCC computed-goto only behind compiler feature checks, with a portable fallback. The backend consumes validated instruction blocks and guest CPU state, runs within a bounded execution budget, and returns typed outcomes (syscall, fault, yield, exit). Define a narrow callback or event boundary for memory accesses and syscalls. No host pointer may be derived from a guest address without validation. Guard opcode indices and block bounds. Differentially test against the Rust backend and QEMU. Optimize only after profiling.

## 6. Acceleration and interoperability

Trames Accel: Rust-managed capability registry, explicit enable/disable, metrics, safe fallback, and versioned native plugin ABI. Built-in signed accelerators may cover VFS, sockets, memory, compression or cryptography; ordinary guest library calls are not automatically intercepted. Downloaded native machine-code plugins are not supported by the baseline signing model.

Trames Bridge: permission-brokered host/guest API via CLI, versioned asynchronous RPC and selected virtual files. Candidate capabilities: clipboard, Files, Share Sheet, notifications, URL opening and device information. Support cancellation, structured errors, user consent and sandbox-safe access. Files integration uses security-scoped resources and coordination where needed. Do not promise full POSIX semantics for cloud file providers.

## 7. Repository and build

Cargo Workspace is the primary build system. Suggested layout:

crates/{trames-core,trames-cpu,trames-elf,trames-memory,trames-linux,trames-vfs,trames-process,trames-accel,trames-bridge,trames-terminal,trames-ffi,trames-cli}/

native/interpreter-cpp/  (CMake or cc/cxx build integration)

apps/ios/  (SwiftUI + Xcode)

tests/{fixtures,differential,integration}/

docs/{architecture,roadmap,security,decisions}/

.github/workflows/

Use cargo fmt, clippy, cargo test, sanitizers where practical, C++ unit tests and Linux CI. Cross-compile iOS static libraries and integrate via an explicit C header and XCFramework or equivalent Xcode-supported artifact. Linux remains the principal developer platform; cloud Mac is needed for iPadOS build/signing and device validation.

## 8. Security and correctness

Treat ELF files, packages, guest pointers, paths and bridge RPC payloads as untrusted. Validate memory bounds, file permissions and syscall arguments. Use RAII/ownership, explicit resource limits, typed errors, deterministic tests and fuzzing for parsers. The emulator is not a hardware isolation boundary: host-facing bugs can affect the containing app. Protect SSH keys, verify host identities and require user consent for host resources. All acceleration must preserve guest-visible semantics; side-effecting operations cannot be blindly retried after partial execution.

## 9. Testing and performance

Unit-test ELF parsing, instruction decoding, registers, memory permissions, syscall validation and ABI boundaries. Differentially execute identical AArch64 fixtures under Rust, C++ and QEMU; compare outputs, exit status and observable memory/register results, normalizing nondeterminism. Track guest instructions/s, block dispatch overhead, memory translation cost, shell latency, package installation time and peak RSS. Publish measured numbers only, with device, build flags and workload. Include malformed ELF and invalid-pointer regression tests.

## 10. Delivery milestones

M0: Cargo workspace, Rust ELF64 loader, guest memory, CPU reference interpreter, write/exit syscalls, static AArch64 Hello World fixture, QEMU differential tests and Linux CI.

M1: wider AArch64 coverage, static BusyBox applets, VFS and additional syscalls.

M2: process model, execve, pipes, signals, virtual PTY and interactive ash.

M3: dynamic linking, musl, Alpine rootfs, networking, apk update/add.

M4 (parallelizable): SwiftUI terminal, native SSH, Files, keyboard, developer-signed iPad build.

M5: Bridge RPC, scoped storage, acceleration registry, benchmarked C++ backend.

M6: Git, Python, GCC/Clang, Make/CMake, build-and-run workflows; Debian later as ABI coverage permits.

Do not treat installing a package as proof it can execute. Verify the installed binaries.

## 11. First Work iteration

Confirm GitHub owner, repository availability, private visibility and license choice before creating the repository. Create main and M0 issue only after approval. Implement functioning code, not only skeleton interfaces. Build with cargo build --workspace and test with cargo test --workspace. Cross-compile a minimal static AArch64 Linux fixture, run it with trames-run and independently with qemu-aarch64, compare output and exit code, and commit verified milestones. Record unresolved gaps and next steps. Never hardcode expected output based on filename. Preserve this charter as docs/charter.md and record ADR-001/ADR-002.
