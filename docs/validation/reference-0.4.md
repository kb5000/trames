# 0.4.0 reference correctness and acceleration

Validation date: 2026-10-12 (Asia/Shanghai). This record will include the final
code/CI/artifact references once those checks finish. The reference backend stays
the default; Linux syscall expansion is deliberately incremental.

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
batch of three paired samples is used; shared-host timing variance and CPU-only
scope will be recorded with the results. These are not iPad app measurements.

## Reproduction

Run the Linux CI matrix for reference and C++ backends, which includes fmt,
clippy, Rust tests, iOS-target checks, all differential suites, ASan/UBSan for
computed-goto and portable dispatch, and actual Alpine workflows. Set
AARCH64_AS/AARCH64_LD/QEMU_AARCH64 when tools are outside PATH.

Representative decode probes can be repeated with the committed
`instruction_probe` example, built with `--features cpp`; it consumes lines
of identifier and hexadecimal opcode and compares backend outcomes. These probes
only establish decode acceptance, not full semantic correctness.
