# Direct guest memory and musl pure-call expansion

Development follow-up, 2026-10-13 (Asia/Shanghai), source
[`96cf231`](https://github.com/kb5000/trames/commit/96cf231ecd8fe9e39e5af6998e55b5f041308fb8).
This is not a new signed device release. The
[queue](../roadmap/musl-optimization.md) describes the implementation and remaining
pure/stateful candidates. The previous 0.10.0/build 11 release record stays intact.

## Functional checks

- 200 workspace tests, strict workspace Clippy and formatting passed. 27 tests in
  memory/native/accelerator crates passed on an actual ARM64 runtime under pinned
  QEMU 10.0.13; core/distro/FFI also compile for aarch64-apple-ios.
- Full musl retains all 1,642 public Alpine exports and hides original fallbacks
  and NativeCall helpers. Three dynamic fixtures exercise activation off/on and
  memory/disk stores, plus persistent standard-path replacement, direct loader,
  invalid-ELF rollback and staging conflicts. Ordinary Linux/QEMU uses fallback.
- The pure fixture exercises all ten IDs, limits, zero sizes, unsigned results,
  tags, alignment, early NUL/mismatch, guard pages, errno, fork, shared VM, exec,
  coherent file aliases and executable writes that actually enter the native
  path. A detached MAP_PRIVATE file page still raises SIGBUS after truncation.
- Scoped-borrow tests check direct slice identity, COW, shared VM, page tokens,
  unaligned overlapping moves against snapshots, backed aliases and permission
  preflight. Reads through live backings and shared writes retain bounded staging.
- The old five-function prototype passes its ten ordinary-ELF/BusyBox cases.
  The current full libc also runs with the previous 0.10 host: absence of the new
  function bitmap selects original implementations for IDs 6–10.
- Existing mmap comparisons, 36 M3 comparisons with authenticated HTTPS/apk,
  20 Linux endpoint/Vim checks, signed Python installation/repair and actual
  math/json/sqlite3/pty, and embedding PTY/Files/lifecycle workflows passed.
- The local full-musl source package was compared byte-for-byte with its ELF and
  every hashed patch input; the original musl archive and MIT/Arm notices remain
  included under GPL-3.0-or-later integration.

## One measured before/after comparison

[Raw samples and identities](musl-optimization-benchmark.json) compare the previous
0.10 host and current ARM64 host using the same complete libc and guest program.
Each mode has three samples; medians include loader/startup and exclude host
rootfs import. The host is x86_64 Linux running the ARM64 runtime under QEMU.
No physical-iPad or general application speedup is established by these results.

| Workload | Previous host, enabled | Current, enabled | Previous/current speed ratio | Current off/on speed ratio |
| --- | ---: | ---: | ---: | ---: |
| 32-byte copy, 256 iterations | 0.0931 s | 0.0881 s | 1.06x | 0.99x |
| 64 KiB copy, 256 iterations | 0.1318 s | 0.0986 s | 1.34x | 10.47x |
| Unmodified Alpine sort, 2,048 rows | 16.1229 s | 18.8003 s | 0.86x | 0.97x |

The bulk-copy result is about 34% faster than the previous enabled transport,
which used two full-size intermediate buffers. The current copy workload reports
direct native reads/writes and no buffered bytes. COW page allocation is still
possible and is not counted as staging.

The sort sample regresses by about 17% against the previous host. A
[single diagnostic pair](musl-optimization-sort-control.json), with acceleration
off in both runtimes, varied in the opposite direction (25.65 s previous,
19.23 s current). These small shared-cloud/QEMU samples do not establish a stable
sort improvement or isolate a runtime regression. Keep the raw regression visible;
repeat controlled application/device measurements at a later meaningful batch.
Tiny operations do not show a useful native-entry benefit.

During development a large strnlen bound over short lines caused 2,048 unnecessary
native entries in sort. A 64-byte local HASZERO prefix removes those entries;
the final sort run has 52 native calls, all from the original copy/fill group.
This is why the queue retains workload-based thresholds instead of wrapping
every libc call indiscriminately. Benchmarks remain optional, not a CI loop.

## CI tracking

[Linux](https://github.com/kb5000/trames/actions/runs/38281357445) passed on attempt 1,
including the signed GCC/Clang/Git/CMake workflows. [Apple](https://github.com/kb5000/trames/actions/runs/38281357373)
passed on attempt 2, including native Rust checks, device/simulator linking and
seven actual iPad simulator tests with zero failures. The downloaded build
artifact's ZIP digest/CRC and [actual test summary](musl-optimization-simulator.json)
were checked. CI's complete-musl artifact was also checked against all pinned
patch/build source bytes, the upstream archive and its packaged ELF digest.
Conclusions and artifact identities are in [the accompanying JSON](musl-optimization.json).
The first Apple attempt hit an existing pinned NIOSSH
channel-window-adjust fatal error in the native OpenSSH/SFTP test. Retrying only
the failed Apple job passed that test without source changes; this is recorded
as a pre-existing asynchronous test/dependency limitation, not a memory-test pass.
