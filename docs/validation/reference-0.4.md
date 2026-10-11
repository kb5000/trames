# 0.4.0 reference correctness and acceleration

Validation date: 2026-10-12 (Asia/Shanghai). Runtime implementation:
[`ddc10bd`](https://github.com/kb5000/trames/commit/ddc10bd83ed6d4c14a3b6ad4bd5678fe04584992).
Local fmt/clippy, all 137 Rust tests, the iOS target check, both complete QEMU
matrices and computed-goto/portable ASan/UBSan passed. The reference backend stays
the default; Linux syscall expansion is deliberately incremental.

[Linux reference/C++ CI](https://github.com/kb5000/trames/actions/runs/38162066667)
passed at `2b78baf`, which changes only oracle/validation files after the runtime
commit above. Both jobs include the pinned oracle, all differential suites,
sanitizers, BusyBox/PTY/disk/native and virtual networking, actual signed
Vim/Python and embedding/Bridge checks. The source fingerprints in
[compatibility-0.4.json](compatibility-0.4.json) match the published runtime.

[Apple CI](https://github.com/kb5000/trames/actions/runs/38162066629) passed at the
same revision: native Swift/OpenSSH checks, unsigned simulator/device builds and
all seven actual iPad simulator tests (0 failed or skipped). The downloaded
[xcresult summary](reference-0.4-simulator.json) reports Passed on iPad Pro
11-inch (M4), iOS 18.5 arm64, macOS 15.7.9. This is software/simulator evidence,
not physical iPad acceptance.

[0.4.0/build 5 unsigned IPA](https://github.com/kb5000/trames/actions/runs/38162066629/artifacts/11711540648)
was downloaded and independently checked: iPhoneOS/arm64 device Mach-O, iPad
support, minimum iPadOS 17, stable org.trames.Trames, executable permissions,
matching packaged checksum, and byte-for-byte agreement with all 36 files of the
actual device app. Size 9,367,789 bytes; SHA-256
`55d958bb6255a1baccc91d332d8cc438c67db8c2ae9c70784f1e0c062f04ca79`.
The IPA artifact expires 25 October 2026 at 18:20 UTC, and build reports expire
18 October. [Verification metadata](reference-0.4-ipa.json) records the details.

The first Apple attempt hit the previously tracked NIOSSH close/window-adjust
trap in [issue #9](https://github.com/kb5000/trames/issues/9). The subsequent
native OpenSSH test passed; that intermittent dependency issue is not fixed by
this CPU update. Physical device acceptance remains in
[issue #8](https://github.com/kb5000/trames/issues/8).

## Independent CPU checks

The same assembled ELF executes in Trames and independent QEMU. New suites
compare destination registers, preserved upper lanes where applicable, FPSR and
NZCV, with edge/random inputs and source/destination aliases. The FP16 suite
exhausts all 65,536 input bit patterns for IEEE and AHP widening; it is not an
exhaustive proof of every arithmetic input pair.

| Suite | Register/flag/memory snapshots |
| --- | ---: |
| `vector_integer.py` | 22,374 |
| `vector_floating.py` | 15,084 |
| `half_floating.py` | 186,992 |
| `atomics_crc.py` | 2,572 |
| `crypto.py` | 6,144 |
| `modern.py` | 19,872 |
| Total of these six suites | 253,038 |

Base suites use QEMU cortex-a53; extension suites use max. Existing integer,
scalar FP, structure, pairwise, widening and copy/shift suites remain in the
matrix. New modified-immediate regressions cover ordinary MOVI/MVNI/ORR/BIC and
S/D FMOV sharing an encoding class with FP16 fixed-point conversion. Actual
Alpine shell startup caught that decode overlap before publishing.

The oracle is pinned to QEMU 10.0.13. Ubuntu's earlier QEMU incorrectly writes
scalar FP16 fixed-point conversion through `write_fp_sreg`, allowing sign-extended
bits 31:16 to leak into the destination (FCVTZS H,H,#16, input -infinity:
`0xffff8000` instead of `0x8000`). The newer decoder clears the vector and writes
the actual element width. See the independent upstream
[8.2 translator](https://github.com/qemu/qemu/blob/v8.2.2/target/arm/tcg/translate-a64.c#L8485)
and [10.0 translator](https://github.com/qemu/qemu/blob/v10.0.0/target/arm/tcg/translate-a64.c#L9249).
The CI failure matched that source-level oracle defect; the runtime is unchanged.
`build-qemu-oracle.sh` builds the upstream 10.0.13 source archive verified by
SHA-256 `4a59b74dac7ead8b162a7c4595dcbbeebe989b90c89cfe6bb4dec5447342f6fa`.

Invalid encodings and readonly/cross-page memory failures are checked for no
architectural side effects. Tests also check pair alignment, source aliases,
LDAPR reservation behavior, code-cache invalidation, COW/shared memory,
virtual counter ordering and cache-maintenance translation. Atomic ordering
suffixes operate within the cooperative interpreter; this does not prove a
physical multiprocessor memory model.

## Actual guest workflows

Alpine 3.22.6 unmodified executables check root uid, `/root` cwd/HOME and
root USER/LOGNAME, `sync`, ps/mount/df/free/top/ip, IPv4/IPv6 ping, runtime files,
signed Vim installation and interactive edit/save/reopen. VFS tests check vda1
stat/mount/sysfs identity, size, partition attributes, root mount ID and raw I/O
returning EOPNOTSUPP. The canonical syscall 81 test writes an open disk file,
syncs, drops the runtime and verifies persisted length/content after reopening.

Signed Python installation and repair use a fresh store with a host descriptor
limit of 128. Execution checks math/JSON/sqlite3/pty and the exact previously
reported pycache path. Dynamic/TLS tests retain upstream package signatures and
trusted certificates. Advertising AES/SHA HWCAP also exercises real library
selection rather than only isolated opcodes.

## Acceleration and benchmark

Correctness checks precede acceleration. Before/after measurements use matched
Cargo release binaries and identical fixed instruction budgets. Each profile
asserts equal final CPU and memory state between the two backends. Only one final
batch of three paired samples was used, alternating process order and pinning both
binaries to the same allowed logical CPU after local tests ended. The baseline
includes this round's completed CPU implementation before memory and decode
optimization; it is not the published 0.3.0 binary.

| Backend / workload | Before median | After median | Median paired speedup |
| --- | ---: | ---: | ---: |
| Reference integer, 12M instructions | 0.2576 s | 0.1885 s | 1.36× |
| Reference load/store, 1M instructions | 0.0780 s | 0.0388 s | 2.01× |
| C++ integer, 12M instructions | 0.0515 s | 0.0533 s | 0.97× |
| C++ load/store, 1M instructions | 0.0770 s | 0.0384 s | 2.08× |

Optimizations remove duplicate page lookups for aligned fetches and single-page
accesses, and skip SIMD/FP decoders for other primary instruction classes. FPCR/
FPSR remain in the system-instruction decoder. Cross-page preflight, permission
checks, COW, shared pages and instruction invalidation keep their semantics.
The C++ integer path is unchanged and shows timing variance, not an improvement.
Rust remains default. Linux x86_64/Xeon Platinum 8573C, Rust 1.90.0/GCC 14.2.0,
release/thin-LTO/overflow checks and cpp feature; raw samples, binary hashes and
profile source hash are in [reference-0.4-profile.json](reference-0.4-profile.json).
Short shared-host CPU loops do not establish general application or iPad speed.

## Reproduction

Run the Linux CI matrix for reference and C++ backends, which includes fmt,
clippy, Rust tests, iOS-target checks, all differential suites, ASan/UBSan for
computed-goto and portable dispatch, and actual Alpine workflows. Set
AARCH64_AS/AARCH64_LD/QEMU_AARCH64 when tools are outside PATH.

Representative decode probes can be repeated with the committed
`instruction_probe` example, built with `--features cpp`; it consumes lines
of identifier and hexadecimal opcode and compares backend outcomes. These probes
only establish decode acceptance, not full semantic correctness.
