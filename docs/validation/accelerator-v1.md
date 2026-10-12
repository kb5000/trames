# NativeCall v1 pure profile — first development milestone

Local verification, 2026-10-12. Implements the initial bounded transport and five
pure functions described in [the v1 contract](../architecture/accelerator-v1.md).
The immutable 0.9.0/build 10 IPA predates this source; next app delivery will be
combined after more accelerator work. Complete CI source/run evidence is recorded
below once the new workflows finish.

- 192 workspace unit tests passed; fmt and Clippy with denied warnings passed.
- Original pinned Arm memcpy/memset execute in the actual ARM64 native crate under
  QEMU 10.0.13. Lengths 0..259 plus 511/512/4095/4096/65536, 16 source/destination
  alignments and fill values 0/128/255 preserve surrounding canaries.
- ARM64 static and dynamic guest binaries compare with the independent QEMU
  oracle, with acceleration on/off in both memory and persistent stores: eight
  combinations passed. Includes actual dlopen/dlsym function-pointer calls,
  overlaps, low unsigned bytes, exact signed comparisons, zero length, tags,
  same-pointer move, 64 KiB limit fallback and strings terminating at page edges.
- Real musl fault handlers verify precise SIGSEGV/SIGBUS data addresses, original
  fallback, COW/fork, CLONE_VM threads, exec activation, anonymous/file shared
  aliases, fd coherence and executable-page decoder invalidation. Malformed
  NativeCall entry stays SIGILL. Local QEMU user lacks ARM binfmt host exec, so
  only that oracle-side exec subcase is skipped; Trames executes the child ELF.
- Unmodified Alpine BusyBox with the actual ELF installed via LD_PRELOAD runs a
  pipe/exec workflow correctly both disabled and enabled; the enabled ARM64 run
  reports 56 native calls and no native faults/rejections.
- NativeCall/reference cached parity, held fault PC/input registers, one-instruction
  retirement, exact budget checkpoints and no-effect invalid/over-limit calls passed.
- Existing real ARM64 mmap, CPU signal/context and dmesg fixtures passed. Current
  audits retain 893 representative CPU encodings (877 decoded, 2 SVC, 2 exceptions,
  12 unsupported) and 161 Linux syscall subsets / 155 missing / 2 obsolete entries.
  NativeCall is a private software protocol, not an added ISA/Linux ABI count.
- iOS ARM64 native rlib is actually code-generated as Mach-O, and the linked
  embedding closure passes the iOS target check. Apple linking/runtime CI follows.

## One-shot performance baseline

Same ARM64 guest binary, 256 iterations, three measurements for each mode; use
median whole-run time including guest startup. The ARM64 Trames runtime itself
runs under the pinned QEMU on this x86_64 development machine. This quantifies
interpreter microbenchmarks, not physical-iPad or application startup performance.
[Raw timings, instruction counts, transfer metrics and fingerprints](accelerator-v1-benchmark.json).

| Workload | Bytes per call | Enabled/off speed ratio |
| --- | ---: | ---: |
| Copy | 32 | 0.87× |
| Copy | 4,096 | 1.50× |
| Copy | 65,536 | 8.96× |
| Copy + compare | 32 | 0.85× |
| Copy + compare | 4,096 | 14.03× |
| Copy + compare | 65,536 | 111.27× |

Small calls regress: entry validation, guest wrappers and temporary buffers cost
more than short interpreted routines. This first version does not add a size
threshold or zero-copy borrow. Those are the next optimization candidates; a
bulk microbenchmark gain alone does not establish a Vim/apk/Python speedup.
The optional benchmark is not run on every CI push.

## Reproduce

```sh
bash scripts/build-busybox.sh
bash scripts/prepare-alpine.sh
bash scripts/build-accelerator-fixtures.sh
cargo build -p trames-cli --target aarch64-unknown-linux-musl --release --locked
TRAMES_RUNNER=tests/differential/arm-runner.sh python3 tests/integration/accelerator.py
# Optional, when establishing a new baseline:
TRAMES_RUNNER=tests/differential/arm-runner.sh python3 tests/performance/accelerator.py
```

The guest package includes its library, full GPL/MIT notices and exact corresponding
source/build script. Native and guest upstream fingerprints are preserved in
[provenance](../../crates/trames-native/upstream/musl-1.2.5/provenance.json). License
and source provenance for the incorporated routines are recorded separately in
[dependencies](../security/dependencies.md).
