# Complete hybrid musl validation — 0.10.0/build 11

Local verification on 2026-10-12, followed by full Linux/Apple CI evidence below.
The [implementation](../architecture/musl-hybrid.md) retains standard loader/libc
paths, wraps five pure functions and keeps the rest of original musl as guest code.

- 1,642 public dynamic symbols: all exports from the pinned Alpine loader remain;
  no Trames private helper is exported. The full ELF also runs as a normal musl
  loader under independent QEMU with NativeCall disabled.
- Actual ARM64 runtime/guest comparisons under QEMU 10.0.13 pass 12 combinations:
  pure-function/fault fixture, startup/constructor/TLS/allocator/I/O fixture and
  dynamic Linux compatibility fixture, each with memory/disk storage and host
  acceleration enabled/disabled. Oracle-only ARM child exec is skipped without
  host binfmt; Trames executes the child ELF.
- Coverage includes dlopen/dlsym, pthread TLS isolation, calloc/realloc/free,
  errno preservation, stdio/variadic formatting, libm, alternate signal stacks,
  membarrier registration, file limits, precise SIGSEGV/SIGBUS, COW/shared
  aliases, executable writes, malformed entry SIGILL and large original fallback.
- Existing-store upgrade/reopen preserves user files, the libc alias and ordinary
  pipe/exec behavior. Direct loader invocation works. Invalid ELF and a preexisting
  sibling staging file leave the current library and user staging file intact.
- Real embedding ABI runs the full library through Alpine PTY, Bridge consent,
  disk transfer, resize, pause/cancel, reopen and explicit reset operations.
- Workspace tests, Clippy and iOS target checks pass. Complete CI also exercises
  the full replacement in authenticated HTTPS/apk, Vim, Python and developer tools,
  and in the actual iPad simulator network/lifecycle path.

## Reproduce

```sh
bash scripts/build-busybox.sh
bash scripts/prepare-alpine.sh
bash scripts/build-musl-hybrid.sh
bash scripts/build-musl-hybrid-fixtures.sh
cargo build -p trames-cli --target aarch64-unknown-linux-musl --release --locked
TRAMES_RUNNER=tests/differential/arm-runner.sh python3 tests/integration/musl-hybrid.py
# Optional single baseline; not part of every CI push:
TRAMES_RUNNER=tests/differential/arm-runner.sh python3 tests/performance/musl-hybrid.py
```

## Performance and delivery evidence

The one-shot baseline compares the same complete musl ELF and guest programs with
acceleration off/on. Three runs per mode use median guest runtime, including
loader/startup and excluding host rootfs import. The ARM64 runtime itself executes
under QEMU on this x86_64 development host. This does not establish physical-iPad
or general application startup performance. [Raw measurements and exact input/source fingerprints](musl-hybrid-benchmark.json)
record the single baseline: 32-byte copy 0.93×, 64 KiB copy 9.02× and unmodified
Alpine sort on 2,048 lines 1.04×. Small-call overhead remains; the modest sort
difference is not evidence of a broad application speedup.

Full CI source/run IDs and the verified unsigned device IPA will be added after
the workflows complete. A physical-iPad acceptance run still depends on owner
installation through Sideloadly; simulator evidence is recorded separately.
