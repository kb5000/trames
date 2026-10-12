# CPU coverage — 0.8 source profile

This batch expands functional AArch64 EL0 coverage and retains one production
Rust cached interpreter. Floating-point, SIMD and crypto compute uses static
ARM64 instructions first, without JIT. The optional C++ executor, tail-dispatch
prototype, runtime selectors and build features are removed. Embedding ABI is
now 3; Cargo/iOS version is 0.8.0, iOS build 9. The earlier 0.7/build 8 artifact
does not contain these changes. Both complete CI workflows and new device-artifact validation pass.

Current CPU and Linux omissions are tracked together in the
[current gaps inventory](../roadmap/compatibility-current.md).

## Implemented additions

| Family | Scope |
| --- | --- |
| CSSC | ABS/CNT/CTZ W/X; signed/unsigned MIN/MAX register and immediate forms |
| FlagM2/system | AXFLAG/XAFLAG, SB, DGH, CLRBHB, RNDR/RNDRRS from OS entropy, counter aliases and TPIDR2_EL0 |
| BF16 | BFCVT/BFCVTN/2, BFDOT vector/indexed, BFMMLA, BFMLALB/T vector/indexed |
| FP widening/complex | FMLAL/FMLSL and upper/indexed forms; FCADD rotations and FCMLA vector/indexed rotations |
| Crypto | SHA512 H/H2/SU0/SU1; SHA3 EOR3/BCAX/RAX1/XAR; SM3 SS1/TT/PARTW; SM4 E/EKEY |
| Ordered/paired memory | LDLAR/STLLR byte/half/word/double; SWPP/LDCLRP/LDSETP with acquire/release variants |
| Bulk memory | MOPS copy, overlap-safe move and set prologue/main/epilogue, temporal/unprivileged hints and zero-tag SETG |
| CPU exceptions | BRK/HLT, SIGILL/SIGTRAP/SIGBUS/SIGSEGV, fault addresses, guest handlers and rt_sigreturn context recovery |

MOPS chooses architectural Option A and yields after at most 64 KiB of main-stage
work. Its registers retain progress, PC stays on the unfinished instruction,
and the instruction is retired only when complete. Ordinary data accesses ignore
the top address byte. Executable address validation, mapped-memory quotas, R/W/X
checks and coherent code invalidation remain in force.

BF16/FHM/complex and extended crypto use optional native host instructions when
available. Older hosts use functional fallbacks. Floating-point fallbacks may
differ in rounding, NaN payloads and exception flags; in particular, BF dot/matrix
fallbacks use native FP32 arithmetic rather than software reproduction of every
BF16 rounding step. Full arithmetic ISA conformance is not claimed.

## Explicit compatibility substitutions

| Instructions | Virtual behavior |
| --- | --- |
| PACIA/B/DA/DB, AUTIA/B/DA/DB, zero/modifier hints | Signing/authentication disabled; values preserved |
| XPACI/XPACD | Strip to the virtual 48-bit address; XPACLRI hint is accepted without authentication state |
| PACGA | Return zero compatibility authentication code |
| BRAA/BRAB/BLRAA/BLRAB, zero forms, RETAA/RETAB | Execute the real jump/return; link forms update LR; authentication disabled |
| LDRAA/LDRAB | Perform the checked load, signed offset and optional writeback |
| BTI | Accept landing hint; landing enforcement disabled |
| IRG/ADDG/SUBG/GMI/SUBP/SUBPS | Zero-tag profile with real address arithmetic and applicable NZCV results |
| LDG/STG/STZG/ST2G/STZ2G/STGP/SETG | Allocation tags zero; checked accesses, zeroing, data stores and writeback execute |
| WFI/WFxT | Allow spurious wakeup; bounded cooperative scheduling remains active |
| LS64 LD64B/ST64B/ST64BV/ST64BV0 | Checked 64-byte normal-RAM transfer; status succeeds; physical Device-memory/ACCDATA semantics absent |

PAC, BTI, MTE, WFxT, LSE128 and LS64 are not advertised as complete Linux hardware
capabilities. Neither authentication/tag-error protection nor hardware timing is
provided. Privileged block-tag operations LDGM/STGM/STZGM remain SIGILL.
SVE/SVE2/SME are still unimplemented; their register state, instructions, Linux
vector-length controls and signal contexts require separate work. Privileged
EL1/EL2 and full-system emulation remain outside this userspace runtime.

## Reproducible evidence

[CSV](instruction-coverage-current.csv) and
[metadata](instruction-coverage-current.json) contain representative encodings,
decode results, compatibility profiles, hardware capability constants and source
SHA-256 fingerprints. They extend the preserved 418-sample 0.7 inventory and
compare single-step acceptance with the cached executor. MOPS probes supply a
valid Option-A main/epilogue entry state. The inventory has 893 samples: 877 decoded, two SVC entries, two synchronous
exceptions and 12 unavailable SVE/SME or privileged tag-operation samples.
This is a decode inventory, not an exhaustive ISA corpus or a percentage of all AArch64 instructions.

```sh
cargo build -p trames-cpu --example instruction_probe --release --locked
python3 scripts/audit-instructions.py
```

Independent semantic oracle: QEMU aarch64 user 10.0.13, `-cpu max`. The ARM64
Linux/musl build of Trames runs under QEMU too, so these tests execute actual
ARM64 compute assembly instead of only the x86_64 development helper.

| New semantic evidence | Cases |
| --- | ---: |
| CSSC/FlagM2 complete GPR/NZCV snapshots | 2,744 |
| BF16/FHM/complex complete vector/FPSR/NZCV snapshots | 9,960 |
| SHA512/SHA3/SM3/SM4 complete vector/FPSR/NZCV snapshots | 5,888 |
| LOR register/memory snapshots | 128 |
| Three-stage MOPS register/NZCV/full-buffer comparisons | 55 |
| Actual musl synchronous fault handlers, edited context and recovery | 7 fault scenarios |

QEMU 10.0.13 has no CSSC or LSE128 decoder. CSSC compares the new instruction with
independent baseline ARM instruction sequences executed by QEMU; FlagM2 uses
the actual instruction. LSE128 and LS64 use result, ordering-variant, aliased
address/status, reserved-register and fault-preservation unit checks. Those are
not direct hardware-oracle comparisons. PAC/tag compatibility has policy-specific
tests rather than comparison with enabled hardware protection. Linux ILLOPC=1
and QEMU user ILLOPN=2 are both accepted by the real-musl SIGILL fixture; the
runtime delivers Linux ILLOPC.

The prior 14 differential suites also exercise the actual ARM64 helper, including
186,992 half-float snapshots, 44,800 FRINTTS snapshots, ordinary integer/NEON,
AES/SHA1/SHA256, memory and atomics. Cross-compilation of the actual compute
library for `aarch64-apple-ios` succeeds; this does not establish iPad throughput.

Local actual-program regression passes: Python selectors/asyncio/subprocess/
threads/SQLite/venv; GCC and Clang compile/link/execute; Git init/commit/merge/gc/
fsck/clone including virtual HTTP egress; Make/CMake/CTest; interactive htop;
adduser/su identity/home/permissions; reopened persistent data. This run reused a
copy of the existing signed tool store, so package installation was not retested
locally. CI retains fresh signed-package installation checks.

Network regression passes 36 QEMU comparisons plus 16 policy/preflight/teardown
outcomes, including real musl DNS UDP/TCP fallback. M6 ABI regression passes
eventfd/epoll/timerfd, Unix socket pairs, POSIX record locks and FIFO behavior.

Final local checks pass: 171 workspace tests, formatting, Clippy with warnings
denied, release build and iOS target compilation. The actual embedding ABI 3
regression passes Alpine PTY/resize, persistent 1 MiB file roundtrip, exclusive
export, pause/cancel/reopen, Bridge consent replies and environment-reset checks.

## One program benchmark batch

Three interleaved baseline/candidate runs per workload, each starting with a
fresh copy of identical signed-package stores. Vim retains defaults/plugins;
Python output and compiled GCC result are checked. Baseline is preserved 0.7
cached execution, candidate is the combined 0.8 single-interpreter/native-compute
source. [Raw timings, binaries and input fingerprints](native-compute-0.8-programs.json).

| Workload | 0.7 median | 0.8 median | Change |
| --- | ---: | ---: | ---: |
| Vim spawn to first cursor | 1.973 s | 1.882 s | about 5% faster |
| Python imports/SQLite/JSON/hash to exit | 7.330 s | 7.259 s | about 1%; effectively unchanged |
| GCC compile/link/execute | 2.736 s | 2.670 s | about 2% faster |

These are Linux x86_64 development-host timings with warm host caches and the
host-specific compute helper. They are a small regression/performance observation,
not proof of ARM64 iPad gains or maximum non-JIT throughput. No physical iPad
measurements were made. This batch prioritizes instruction functionality.

## Verified combined delivery

Runtime source: `5d6f1076297385a0d120ff68b02a8c759d3a0db2`.
[Linux CI](https://github.com/kb5000/trames/actions/runs/38236217253) and
[Apple CI](https://github.com/kb5000/trames/actions/runs/38236217045) both pass
on that exact commit. [CI and artifact metadata](instruction-0.8-ci.json).

The complete Linux run includes actual ARM64 instruction/libm/fault/ABI checks,
network/storage/PTY endpoints, signed apk authentication, fresh Python install
and repair with 128 host descriptors, and fresh signed Git/Python/GCC/Clang/
Make/CMake/htop installation plus complete developer workflows. The long package
and tool workflows use the x86_64 development-host executable; strict numerical
and instruction comparisons use the ARM64 build under QEMU. Guest-produced
AArch64 binaries also execute independently under QEMU. No ISA criterion was
relaxed to accommodate native x86 NaN differences.

The simulator report records seven passes, zero failures/skips on arm64 iPad Pro
11-inch (M4), iOS 18.5; [actual test summary](instruction-0.8-simulator.json).
Native SSH PTY/SFTP checks also pass. The known intermittent upstream NIOSSH
issue and physical-device acceptance remain separate work.

[Download 0.8.0/build 9 unsigned IPA](https://github.com/kb5000/trames/actions/runs/38236217045/artifacts/11741258008).
The extracted IPA is 9,427,233 bytes with SHA-256
`d9c8513c286f89962f58d48f0343579a69ed58c487d7a7df61e3e176cc51a675`.
It retains `org.trames.Trames`, iPadOS minimum 17, and v3 disk data. The ARM64
Mach-O targets the iOS device platform and retains executable permission.
All 36 IPA files match the actual device `.app` artifact, and the artifact/IPA
checksums match. [Verification](instruction-0.8-ipa.json).

Unzip the artifact and use Windows Sideloadly with the same Apple ID/effective
bundle ID to preserve data. This is one combined install update. No data reset,
new signing entitlement or JIT activation is required. Simulator tests and Linux
host measurements do not establish physical iPad throughput.
