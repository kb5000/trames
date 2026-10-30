# Trames Accelerator — Guest ABI-Compatible Native Acceleration

> **Status:** RFC; initial opt-in phases A/B implemented in development; later phases proposed
> **Date:** 2026-10-12  
> **Repository path:** `docs/architecture/accelerator.md`  
> **Scope:** Linux AArch64 guest; portable host backends; no-JIT baseline; optional future JIT  
> **Naming:** “Trames Accelerator” is the umbrella name; “Guest Native Library” denotes a guest-side stub ELF; “Native Backend” denotes precompiled host implementation.

Repository integration note: this owner-provided RFC was added as a future proposal
in 0.9.0. Subsequent development implements the limited [NativeCall v1 pure profile](accelerator-v1.md);
the full libc/device design remains proposed. The immutable 0.9.0 IPA does not
contain this follow-up. The source document SHA-256
is `c89ea4adf4bbe89e6dbfbdb34328d222dd73cd8b7bbe51a2e4f93b51e2842b31`.

The selected [VA-API / VideoToolbox integration](vaapi.md) records the current
video architecture and native shared-plane feasibility results. Guest VA-API
decoding remains pending.

## 1. Summary

**Trames Accelerator** is an ABI-preserving native execution facility for Trames's Linux AArch64 userspace. A selected **guest** shared library (`.so`) is replaced with an ABI-compatible **guest ELF stub library**. Its entry points use a reserved Trames virtual-CPU instruction (a *native-call trap*) to dispatch to a **host-native implementation** already compiled into the Trames application. Native logic executes on the host CPU or appropriate hardware framework, while guest-visible pointers, process state, allocator metadata, TLS, and libc state remain in the **guest** address space.

The initial scope is fixed, explicitly supported builds of musl and glibc; later backends can expose Linux Vulkan, VA-API, and ONNX Runtime interfaces over the same native-call transport. The first [Vulkan compute profile](vulkan-compute.md) now implements selected standard guest ICD calls and native driver resources, with graphics deferred. Host-managed GPU/video/ML hardware objects are represented by guest-safe handles. The baseline iPadOS release does not download host executable code or require a JIT. A future optional AArch64-to-AArch64 dynamic translation backend may implement the *same* native-call semantics where platform permissions allow it.

**Key distinction:** installing guest stubs is **not** installing iPadOS dynamic libraries. Guest stubs are AArch64 **Linux ELF input** to the Trames runtime; native backend code ships as part of the signed host application.

### Conceptual execution path

```text
Unmodified Linux AArch64 application
             │ ordinary ELF symbol/PLT/function-pointer call
             ▼
   Guest ABI-compatible .so stub  ───────► optional guest implementation/fallback
             │ native-call virtual instruction
             ▼
        Trames CPU exit event
             │ capability + ABI + entry validation
             ▼
      Trames Native Dispatcher
             │ validated guest memory/handles
             ▼
  Precompiled host-native backend
    ├─ musl/glibc logic: guest-owned state and guest pointers
    ├─ Vulkan: host Vulkan/MoltenVK/Metal resources
    ├─ VA-API: VideoToolbox/CoreVideo resources
    └─ ONNX: Core ML model execution (ANE if supported/selected)
             │ result / precise error / async completion
             ▼
        Resume guest caller
```

## 2. Repository baseline and historical meaning

This proposal **extends** an existing name; it must not rewrite historical milestones as if this facility already shipped.

| Source | Verified current / historical status | Relationship to proposal |
|---|---|---|
| [`docs/charter.md`, §6](../charter.md) | *Trames Accel* envisages a Rust-managed capability registry, opt-in, metrics, fallback, and versioned native plugin ABI; explicitly excludes automatic ordinary-library interception. | Naming/management origin; proposed guest stub mechanism changes the interception model. |
| [`docs/architecture/m5.md`](m5.md) | Existing Bridge `accel.list`, `accel.enable`, `accel.disable`, `accel.sha256`: built-in SHA-256; registry ABI **1**. Explicitly **not** libc interposition or a native plugin SDK. | Preserve as a legacy Bridge-facing management/diagnostic API; do not confuse its ABI version with the new native-call ABI. |
| [`crates/trames-accel/src/lib.rs`](../../crates/trames-accel/src/lib.rs) | Implemented registry contains enable state and calls/bytes/nanoseconds/fallbacks for SHA-256. | Potential starting point for typed backend registration and separate native-call metrics. |
| [`docs/architecture/dynamic-userspace.md`](dynamic-userspace.md) | Current dynamic loader uses guest ELF interpreter and guest musl; explicitly does **not** substitute library functions. | New replacement is optional and future-only; loader behavior remains unchanged by default. |
| [`docs/architecture/nonjit.md`](nonjit.md), [`ADR-003`](../decisions/ADR-003-static-native-compute.md) | Current production CPU is **one Rust cached interpreter** with static ARM64 compute helpers. Historical C++/tail-dispatch alternatives have been removed; no JIT is implemented. | Native-call CPU exit must first work in this interpreter. Future JIT requires a new ADR and does not retrospectively override ADR-003. |
| [`docs/roadmap/compatibility-current.md`](../roadmap/compatibility-current.md) | Current Linux ABI gaps include the 0.9.0 shared-mapping/logging update; prior 0.8.0/build 9 evidence remains historical. | Native libc must not silently bypass these compatibility limits or assert unsupported kernel semantics. |

**Do not conflate:** (1) `trames-compute` static ARM64 arithmetic helpers; (2) M5 `accel.*` explicit Bridge SHA-256; (3) the **proposed** accelerator hypercall and ABI-compatible guest `.so` replacement. The proposal adds a third mechanism, potentially reusing management and telemetry infrastructure.

## 3. Goals and non-goals

### Goals

1. **Unmodified application binaries.** Standard Linux ARM64 ELF programs continue to resolve documented libc/device-library symbols using the guest dynamic linker, including calls via `dlsym`/function pointers.
2. **Guest ABI fidelity.** Match a finite, pinned set of exact libc/loader builds, including exported/hidden symbols as needed, symbol versions, TLS, data objects, allocator layout, and behavioral contracts.
3. **Native logic, guest state.** musl/glibc allocator and runtime state stay in guest memory; native backend logic operates through validated `GuestMemory` interfaces. `fork`, `CLONE_VM`, `execve`, signals, and COW keep their existing guest semantics.
4. **One native-call transport.** Interpreter and any future JIT backend deliver the same typed exit. Libraries and device APIs share discovery, versioning, authorization, metrics, and return conventions.
5. **Signed native implementation.** iPadOS binaries contain precompiled native backends; downloadable guest packages contain only ELF stubs, manifests, and ordinary guest data/code subject to platform distribution review.
6. **Optional optimization.** Unsupported/mismatched profiles do not corrupt guest state. Where a known-correct standard guest library exists, it remains available as a fallback.
7. **Host portability.** Guest ABI and native-call semantics do not depend on Metal, Darwin, or a specific host implementation.

### Non-goals (initial versions)

- Emulating Linux kernel privileges or extending sandbox permissions through a trap.
- Transparent acceleration of arbitrary unmatched glibc/musl builds.
- Runtime downloading/loading of unsigned iPadOS machine code.
- A CPU JIT in the App Store baseline, or a JIT implementation as part of this RFC.
- Full Vulkan/VA-API/ONNX coverage in the first milestone.
- Automatically replacing statically linked libc calls or hidden direct calls not routed through registered stubs.
- A claim that exposing a guest executable package automatically satisfies App Store policy.

## 4. Package and replacement model

### 4.1 Unit of distribution

Each accelerated profile is distributed as a **guest package** containing:

```text
trames-guest-libc-<profile>/
  lib/.../libc.so.6                    # or musl libc.so, ABI-preserving guest stub
  lib/.../ld-linux-aarch64.so.1        # only if this *exact* profile requires it
  usr/share/trames-accel/<profile>.toml
  usr/share/doc/trames-accel/...       # optional notices/licenses
```

Paths are illustrative, not a claim about Alpine/Debian packaging layout. Different ecosystems can use `apk`, `deb`, or other guest-side package formats. Do not overwrite a system libc as an uncontrolled post-install side effect: use atomic package transactions or an opt-in profile activation mechanism, with rollback to an ordinary guest libc/rootfs.

The loader and linker continue to run **inside the guest**. Stub packages preserve expected ELF sonames, relocation behavior, versioned symbols, function and data addresses, TLS, and constructors/destructors as required by that profile. `dlsym` must return a valid **guest** address, never a host function pointer. Some internal functions and loader phases may have to remain guest code until they can be faithfully bridged.

### 4.2 Exact build identity

A profile match MUST include more than a marketing version such as `glibc 2.x`. Record at least:

- family (`musl`, `glibc`, `vulkan`, `vaapi`, `onnx`), target architecture, OS ABI;
- exact upstream release, distribution patches/build identity, required ELF Build-ID or content digest(s);
- **stub ABI revision**, **native-call protocol revision**, native backend compatibility range;
- exported and required hidden symbols, ELF symbol versions, TLS/data layout fingerprints, dynamic-linker dependencies;
- compiled host backend ID and supported features, with a hash/signature or immutable build mapping where appropriate.

Prefer build-time generation of manifest/symbol inventories and reproducible stubs from pinned sources. Exact identity prevents a host-side implementation from reading an incompatible guest allocator or TLS layout. The manifest is **not a security authority** merely because it sits inside guest VFS; the host validates against its signed-in supported profile catalog.

### 4.3 Upstream source and original-code reuse

The repository is GPL-3.0-or-later. The owner explicitly requests reusing original
upstream source and guest code wherever practical, rather than rewriting mature
algorithms without need. For every pinned profile:

- Prefer original implementation sources, tests, tables and AArch64 algorithms;
  preserve copyright, license notices, source provenance and the required
  corresponding source/build instructions for redistributed implementations.
- Compile suitable upstream code into the signed native backend with a narrow
  guest-memory/virtual-kernel adaptation layer. Do not assume that recompiling
  libc unchanged makes host pointers, globals, TLS or allocator state guest-compatible.
- Preserve original guest ELF sections/internal routines and an exact original
  library fallback wherever native adaptation is incomplete or not worthwhile.
  Pure functions can be accelerated before replacing stateful library components.
- Keep the original pinned implementation as the differential oracle and record
  local patches. License obligations of reused components remain documented with
  those sources, including any LGPL-specific requirements for libc components.

This is a source-reuse policy for future work, not a new implementation in 0.9.0.

### 4.4 Fallback and activation

- **Before activation:** validate profile + native backend + entire required ABI closure. If unsupported, continue with the untouched conventional guest library rather than binding a nonfunctional libc.
- **During activation:** avoid mixed incompatible allocator/loader implementations. Partial acceleration is allowed only for independent functions proved ABI-equivalent with a known-good fallback.
- **After entry:** on an incompatible profile or unauthorized call, reject without side effects. Do **not** blindly retry side-effecting native operations after partial execution.
- **After package update:** invalidate registered stub identities/entry ranges and revalidate before use.

## 5. Native-call instruction and protocol (proposed)

### 5.1 Virtual instruction selection

The first [pure v1 implementation](accelerator-v1.md) allocates HLT #0x5452
under ADR-007 and defines its narrow scalar/aggregate ABI. The wider protocol
below remains a proposal for later profiles.

The guest stub emits a designated **AArch64 trap instruction**, decoded as `NativeCall` by Trames, rather than causing a real host CPU trap. An `HLT #imm16` encoding is a **candidate**, not a finalized instruction allocation. Exact encoding, immediate, feature gating, trap precedence, and guest exception compatibility require a separate instruction-encoding review. Reserve a fixed namespace or explicitly validated stub entrypoint; do not reinterpret all guest `HLT` instructions as accelerators.

**Current code** handles `BRK`/`HLT` as guest traps in `crates/trames-cpu/src/lib.rs`; the new operation needs explicit decoder, cached-interpreter, and scheduler support. On unregistered/general guest code, `HLT` retains its guest fault semantics. If the executable is run under an ordinary Linux kernel rather than Trames, the stub is not expected to work unless a separately designed fallback is present.

### 5.2 Illustrative register ABI (not committed)

```asm
// Guest AArch64 stub, design sketch only
// x0..x7: AAPCS64 ordinary arguments; x0: ordinary result
// x16: native module handle (or negotiated module ID)
// x17: native function ID
trames_memcpy:
    mov x16, #1
    mov x17, #1
    hlt #0x5452       // example candidate; needs reservation review
    ret
```

The native-call protocol is **not** a Linux syscall. It MUST not be dispatched through Linux syscall numbering or infer capabilities from guest `x8`. The proposed `x16`/`x17` are call-clobbered under AAPCS64, but the final ABI must explicitly document all caller/callee-saved registers, SP alignment, FP/SIMD arguments and results, NZCV, TLS state, and precise PC semantics. Variadic functions and aggregate/vector calling conventions require typed interface descriptors, not the assumption that eight integer registers suffice.

For an asynchronous call, `NativeCall` returns a continuation/wait state instead of blocking the entire virtual kernel thread; do not hold guest memory borrows or scheduler locks across async suspension. If a native backend calls back into guest code, the reentry mechanism must be explicit, bounded, and maintain a recoverable guest call frame.

### 5.3 Typed dispatcher and execution contract

Suggested layers (interfaces are sketches, not current Rust API):

```rust
struct NativeCall {
    module: u32,
    function: u32,
    abi_revision: u32,
    guest_pc: u64,
}

enum NativeResult {
    Return { /* typed guest registers and optional FP/vector results */ },
    GuestFault { /* exact fault class and guest address */ },
    Pending { /* opaque scheduler continuation ID */ },
}
```

The CPU backend owns instruction retirement accounting and creates an exit with precise pre-call state. The dispatcher validates execution origin, profile identity, module/function/ABI, guest argument types, memory ranges, and resource handles. A native backend can only use explicit guest-memory, virtual-kernel, or approved device interfaces. Result commit/PC advance is atomic with respect to the declared guest-visible effects **where required by the target API**; Linux fault and partial-write semantics must be preserved for operations that are not atomic. Never leak a Rust panic, C++ exception, Objective-C exception, or host pointer across the ABI.

**Failure semantics:** unsupported accelerated entry produces a documented guest-visible failure consistent with the function contract or disables activation before invocation; it must not silently return a fake success. Raw host `errno` and `struct` layouts must not be exposed as Linux ABI.

## 6. Guest memory, process semantics, and state ownership

**Rule:** guest libc state is authoritative **inside guest memory**. Native musl/glibc implementations must reproduce the chosen library's algorithms and observable state transitions, not call Darwin `malloc` and return a host pointer.

- Every guest address is validated and resolved through Trames `Memory`, including access rights, COW, range-overflow checks, tagged-address rules and expected alignment.
- For write operations, COW must occur **before** exposing a writable guest slice; code-page writes must invalidate decoded/JIT translations exactly as ordinary guest stores do.
- Range borrows are short-lived and cannot outlive remap/unmap/protection changes, scheduler yield, callbacks, or async tasks. Cross-page memory uses bounded segmented access with explicit failure/partial-effects semantics.
- `fork` creates an independent COW snapshot of allocator metadata/TLS/global state; `CLONE_VM` shares it; `execve` replaces it. Native implementations keep **no authoritative per-process libc state** in hidden host globals.
- Thread-local `errno`, cancellation, mutex/futex, atomics, robust-list behavior, signal masks and signal unwinding must follow the chosen guest implementation and Linux ABI; cooperative scheduling does not by itself prove lock/atomic equivalence.
- Device libraries differ from libc: actual driver resources (Metal objects, Core Video buffers, compiled Core ML models) necessarily live on the host. Guest receives opaque versioned handles and precisely defined map/sync semantics, never raw host addresses.

### Library-family-specific fidelity

**musl:** exact release/build, allocator family and private metadata, dynamic loader, TLS, syscall wrappers, cancellation points and libpthread-visible state.

**glibc:** precise distro build; `ld-linux-aarch64.so.1`/`libc.so.6` internal ABI; symbol versions, IFUNC, audit/interposition interactions, TLS/TCB/DTV, ptmalloc/tcache/arenas, NPTL, locale and `FILE` layout. The dynamic loader is part of the compatibility contract. A public API match alone is **not** sufficient.

**Vulkan:** guest `libvulkan.so.1` entrypoints/ICD-facing ABI, `vkGet*ProcAddr`, extension/profile negotiation, `pNext` chains, dispatchable/non-dispatchable handles, queue order, mapped memory, fences/semaphores and window-system integration. Host backend may use MoltenVK/Metal; only capabilities actually supported by host are advertised.

**VA-API:** guest `libva`/driver ABI, profiles, picture/slice parameters, surface lifetime, synchronization, image export/import. VideoToolbox is not automatically a per-function equivalent; codecs may need bitstream adaptation.

**ONNX:** prefer an Execution Provider or a stable library-boundary bridge rather than replacing every ONNX Runtime internal symbol. Core ML decides whether an op/model executes on ANE, GPU, or CPU; do not promise NPU execution for all graphs.

## 7. Trust, capabilities, and distribution

1. The native-call opcode is **not a privilege boundary**. Any guest program may contain matching instruction bytes. A native call must remain safe even if invoked by untrusted guest code; entry-range/profile validation is defense-in-depth, not sole authorization.
2. No arbitrary guest pointer becomes a host pointer. Host resources are represented by scoped, generation-tagged opaque handles with lifecycle and ownership checks.
3. Guest root privileges and a matching stub library do not grant host authority. Host effects use the Bridge broker and its current capability policy; a native-backend fast path must not bypass it. The owner-selected text clipboard policy in the 0.18.0/build 21 test app is automatic read/write, with iPadOS controlling paste permission and the global Native Bridge switch able to reject new requests. URL, device-information and scoped Files requests retain explicit per-request consent. Arbitrary host filesystem, keychain, camera and microphone access are not granted.
4. Apply per-call resource ceilings and accounting: bytes accessed, handles, outstanding async requests, CPU time, recursion depth; preserve cancellation and bounded queues.
5. Guest-installed stubs are data/ELF interpreted by Trames; **host native implementations ship compiled and signed with the app**. Baseline iPadOS has no downloaded unsigned executable plugin system. Platform distribution/code-execution policy still requires separate validation.
6. Validate source-license obligations for every redistributed library and incorporated native implementation (musl/glibc, MoltenVK, etc.); version-specific symbols and logic may have separate licensing requirements.

## 8. CPU backends and host portability

The existing **Rust cached interpreter** is the first mandatory implementation. It maps the reserved virtual instruction to a typed `NativeCall` exit, with consistent PC/state and instruction-budget behavior. Normal Linux `SVC` remains separate. A future optional JIT/DBT (for environments that legally/technically allow executable code generation) must implement **the same** native-call event and memory/exception semantics. There is no current production JIT selector; adding one requires a separate ADR after ADR-003.

The dispatcher exposes host-independent backend traits and a negotiated capability set. iPadOS can route Vulkan to MoltenVK/Metal, video to VideoToolbox, and compatible models to Core ML; other hosts may provide different native APIs or no acceleration. The guest `.so` contract should be portable across hosts, but a backend's *capabilities*, errors, resource formats, and performance may differ.

Avoid conflating the proposed Native Accelerator with the existing static `trames-compute` helpers. The latter accelerate individual **instruction semantics**, not entire guest ABI library functions.

## 9. Control plane, observability, and compatibility

Retain the existing `/dev/trames-bridge` `accel.*` API as an explicit administrative/diagnostic surface. Consider extending `accel.list` (through a **new schema revision**, not a silent meaning change) to report:

- backend/profile ID and family, guest ELF Build-ID/digest, ABI/protocol versions, enabled/disabled/incompatible reason;
- native-call count, guest bytes read/written, copies induced by COW, guest-to-host transfer, native execution time and total bridge overhead;
- fallbacks, validation failures, guest fault counts, outstanding async requests and backend-specific capability flags.

Disablement must have defined behavior: choose the ordinary guest library *before binding*, or route particular registered functions to a compatible guest fallback. Disabling an active libc backend in the middle of a stateful process cannot silently switch allocator implementations.

The two interfaces are versioned separately:

- **Bridge registry ABI v1**: already implemented, SHA-256-oriented; historical.
- **Native-call ABI v1 (proposed)**: not implemented; instruction trap, dispatch, memory/handle contracts.
- **Guest library profile revision**: exact libc/runtime build and stub package mapping.

## 10. Implementation roadmap and acceptance criteria

| Phase | Deliverable | Exit criteria |
|---|---|---|
| **A — Transport** | Virtual `NativeCall` event, typed dispatcher, feature negotiation and limits | Interpreter vs reference fixture equality for call/return, invalid IDs, memory faults, signals and budget accounting; no host trap or JIT |
| **B — Pure functions** | Guest stub package for pinned `memcpy`, `memmove`, `memset`, `memcmp`, `strlen` subset | Symbol resolution and function-pointer calls work; boundary/COW/page-crossing, aliasing and invalid-pointer differential tests pass; performance measured with/without bridge |
| **C — Stateful musl** | One pinned musl build with allocator, TLS and internal symbols as necessary | `malloc/free/realloc/calloc`, `fork/CLONE_VM/execve`, multithread locks and heap stress pass against the exact original musl build; no host allocator pointers |
| **D — Pinned glibc** | One exact glibc+loader distribution build | Symbol-version/IFUNC/TLS/linker/allocator and program tests pass; unsupported build remains on ordinary guest libc |
| **E — Native devices** | Headless Vulkan compute, VA H.264 decode, ONNX small-model inference | Verifiable correctness vs native/guest reference; capability negotiation, handles, async/sync and resource teardown tested |
| **F — Alternate CPU backend** | Optional AArch64-to-AArch64 DBT/JIT (separate ADR/distribution) | Differential CPU/hypercall/exception suite passes; permissions and platform requirements explicitly recorded |

**Baseline performance harness:** collect guest instruction count, accelerator calls, end-to-end wall time, native time, guest memory bytes, RSS and fallback rate, with host model, OS version, build flags, and comparable cold/warm runs. Prioritize *workload-level* improvements (Python startup/import, `apk` installs, build tools, compression, hashing) over microbenchmark-only claims.

**Must-pass regression categories:** hidden/internal calls, `dlsym`, `dlopen`, symbol versioning, libc unload/reload, fork-with-threads, shared memory, interrupted native call, signals while pending, cancellation, revoked host permission, malformed/malicious guest stub, mismatched profile, unsupported backend, JIT-vs-interpreter parity where available.

## 11. Open design decisions

1. **Trap opcode:** final instruction encoding, distinction from ordinary guest `HLT`/`BRK` and signal behavior; verify ARM architectural constraints before committing bytes.
2. **Dispatcher layout:** numeric module/function identifiers vs per-profile negotiated handles; stable caller register contract for scalar, aggregate, SIMD, variadic and async functions.
3. **Libc transition plan:** replacement of whole libc+loader vs carefully scoped partial replacement; guest fallback and rollback semantics.
4. **Guest memory fast path:** validated short-lived slice API that safely handles page crossing, COW, executable-page mutation and aliasing.
5. **Callback/reentrancy:** bounded nested native-to-guest callbacks and signal interruption, with no lock-held deadlocks.
6. **Host packaging:** static link vs signed bundled frameworks, per-platform integration, and distribution-review evidence.
7. **Profile toolchain:** generation of stub ELF symbols, Build-ID allowlist and ABI layout tests from pinned upstream/distribution sources.
8. **Device resource model:** common capability/handle/async primitives without forcing GPU/video/NPU state into guest-owned libc memory.

## 12. Initial documentation integration (historical 0.9.0; full design still proposed)

- This RFC is linked from [`docs/roadmap/README.md`](../roadmap/README.md) as a **future proposal**, without claiming implementation in 0.9.0.
- A short forward-reference under [`docs/architecture/m5.md`](m5.md) states that the historic SHA-256 Bridge accelerator is separate from the proposed guest library accelerator.
- A present-tense qualifier in [`docs/architecture/dynamic-userspace.md`](dynamic-userspace.md)records: *“The current runtime does not substitute guest library functions; an optional design is proposed separately.”*
- Do not edit accepted [`ADR-003`](../decisions/ADR-003-static-native-compute.md) to pretend that JIT exists. Introduce a new future decision record if/when multiple production CPU backends are implemented.
- Track native acceleration validation separately from Linux syscall coverage and ISA decode samples.

---

**Design principle:** *Guest ABI preserved; guest state authoritative; native logic accelerated; host privilege unchanged.*  
**产品一句话：** 保留 Linux 二进制接口，通过可替换的 Guest `.so` 桩库，把函数执行交给签名内置的原生后端；解释器与未来可选 JIT 共享同一套加速协议。
