# Native musl floating parsing

Development follow-up, 2026-10-13 (Asia/Shanghai), source
[`b0b5cec`](https://github.com/kb5000/trames/commit/b0b5cecdb3e1200828be003ac2e9b84490ebe4d0).
This is not a new signed device release. See the
[protocol and original-source adaptation](../architecture/native-numeric.md).

## Result and verification

strtod/strtof use the pinned musl floatscan algorithm with hardware binary64
intermediates. Original atof and locale aliases forward to the same public entry
points. strtold retains original guest binary128. The complete libc stays at
standard paths and retains all 1,642 public Alpine exports, with private helpers
hidden. The new exact NativeCall module is gated by existing pure acceleration.

- 207 workspace tests, strict Clippy and formatting passed; 34 memory/native/
  accelerator tests passed in the actual ARM64 runtime under pinned QEMU 10.0.13.
  New tests verify tagged end pointers, page boundaries, untouched fallback output
  and ambient host errno/FP-control/status restoration. Core/distro/FFI compile
  for aarch64-apple-ios using freestanding clang/Mach-O parser objects.
- 712 numeric cases run against original musl/QEMU in four activation/store
  combinations. Observed nearest values are bit-identical, despite a one-ULP test
  allowance; errno and end positions are exact. Cases include malformed input,
  decimal/hex, infinity/NaN payloads, overflow/subnormals, locale aliases, explicit
  rounding fallback, guard/page boundaries and input beyond the 64 KiB ceiling.
- Actual guest tests cover threads/TLS errno, a shared user-supplied pthread stack,
  signals, fork and native qsort → ordinary guest comparator → native strtod.
  Original binary128 strtold preserves its least significand bit in the fixture.
- Existing full-musl 16 fixture/activation/store combinations and persistent
  loader replacement, user data, direct loader and invalid-ELF rollback pass.
- The legacy prototype, 36 M3 authenticated HTTPS/apk comparisons, 20 Linux
  endpoint/ICMP/Vim checks and embedding PTY/Files/lifecycle checks pass locally.
- The native object's undefined symbols are double math, host errno and its
  private scan entry; no software binary128 arithmetic/conversion helper is
  referenced. The original source and helpers remain byte-identical with pinned
  fingerprints. Full guest-musl source-package patch/build inputs match this
  commit exactly and retain the original archive and GPL/MIT/Arm notices.

Default conversion uses native binary64/binary32 at nearest rounding. Explicit
guest rounding directions select original code. Internal native FP exception
flags are not synthesized into guest status; this batch does not claim strict
binary128/exception-flag semantics. Host state is restored, and guest errno/end
stores occur in ordinary guest code after native return. Long/page-crossing
inputs may use bounded staging; short resident input borrows guest bytes directly.

## One concentrated benchmark batch

[Raw samples, source and binary identities](native-numeric-benchmark.json) record
three alternating samples per mode with the same full libc and guest programs.
The preceding published runtime was rebuilt from
[`bb9e2e3`](https://github.com/kb5000/trames/commit/bb9e2e34c5bf4c9253529ba66313c8c9252d6088).
It lacks numeric auxv and uses original conversion while retaining the same pure
strings and callback sort. No implementation input was replaced during the batch.

| Workload | Previous, pure | Current, pure | Current, off | Previous/current ratio |
| --- | ---: | ---: | ---: | ---: |
| snprintf + strtod, 4,096 conversions | 6.1787 s | 1.6519 s | 6.2482 s | 3.74x |
| 2,048 integers, four qsort rounds | 3.2214 s | 3.1495 s | 3.3893 s | 1.02x |
| Unmodified Alpine sort -n, 2,048 rows | 15.7915 s | 5.6558 s | 18.0761 s | 2.79x |

Times include guest loader/startup and exclude host rootfs import. The workload
executes the actual ARM64 runtime under QEMU on shared x86_64 Linux, not an iPad.
These medians establish a useful improvement for these measured inputs, not a
physical-device or general system gain. The current-off sort sample also differs
from the preceding original-parser sample; keep that host/order variation visible.

The integer control is essentially unchanged. Numeric parsing is the large new
gain in BusyBox's existing comparator; the earlier qsort-only batch did not show
an overall sort improvement. This validates the choice to remove expensive
software-wide conversion rather than interpreting byte-count reductions as speed.

## CI tracking

[Linux](https://github.com/kb5000/trames/actions/runs/38328726205) and
[Apple](https://github.com/kb5000/trames/actions/runs/38328726200) passed on
attempt 1. Linux includes the new numeric corpus and signed Python/GCC/Clang/Git/
CMake guest workflows. Apple includes native Rust parser/memory/core checks,
device/simulator links and seven actual iPad simulator tests with zero failures.
The downloaded build ZIP digest/CRC, [actual summary](native-numeric-simulator.json)
and both bundled libc hashes were verified against the checked complete-musl
artifact. Job steps, source/ELF/notices checks and artifact identities are in
[the accompanying JSON](native-numeric.json).
