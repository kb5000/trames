# Resumable musl sorting and nested guest callbacks

Development follow-up, 2026-10-13 (Asia/Shanghai), final source
[`c58e98b`](https://github.com/kb5000/trames/commit/c58e98b927d991aa8ea306db49851a0c523dd846).
This is not a new signed device release. The [protocol](../architecture/native-callbacks.md)
and [ADR-008](../decisions/ADR-008-resumable-guest-callbacks.md) explain ownership,
limits and scheduling.

## Result and functional checks

qsort/qsort_r now use bounded native Smoothsort steps for supported arrays.
Ordinary guest code executes comparators, including nested sort/search/string
calls. Progress lives in the guest stack; no suspended host sort stack, operation
handle or borrowed page survives a step. Public bsearch also has a native backend,
with explicit experimental routing; original musl remains its default.

- 203 workspace tests, strict Clippy and formatting passed. 30 memory/native/
  accelerator tests passed in the actual ARM64 runtime under pinned QEMU 10.0.13.
  Core/distro/FFI compile for aarch64-apple-ios.
- Full musl retains all 1,642 Alpine public exports with private helpers hidden.
  Four dynamic fixtures run with acceleration off/on and memory/disk stores:
  16 combinations, plus persistent loader replacement, user-data preservation,
  direct loader execution, invalid-ELF rollback and staging conflicts.
- The callback fixture's original/native comparison trace is exactly
  `musl-callbacks-ok 8058 3348b8fac6b98905`. It checks varied widths, duplicates,
  record integrity, GNU qsort_r context, nested calls, two threads, alternate-stack
  signals, shared pthread stacks, fork mid-comparison, exec routing inheritance,
  blocking pipe reads,
  siglongjmp and successful subsequent sorting. The explicit native run records
  42 native sorts, 118 searches, pre-mutation fallbacks and zero faults or
  rejected entries. Native Smoothsort record rotations use direct memory access;
  original shared-stack fallback retains the existing staged paths when needed.
- Oversized, live shared and read-only arrays select original code before native
  mutation. Backend tests check malformed progress, arithmetic limits and large
  record stripes. The previous 0.10 host runs the same full library and fixture
  with the identical trace; absent module advertisement selects original sorting.
- The legacy prototype, 36 M3 QEMU/HTTPS/apk comparisons, 20 Linux endpoint/ICMP/
  Vim checks, and embedding PTY/Files/bridge/lifecycle workflows passed locally.
- Pinned upstream qsort.c, qsort_nr.c, bsearch.c and atomic.h remain byte-identical.
  Local and CI full-musl packages retain the original archive, complete patch/build
  inputs, packaged ELF and GPL/MIT/Arm notices. Every packaged input was compared
  with this source commit, and the CI artifact ZIP digest/CRC were checked.

## Measurements and limits

[Raw samples and binary/source identities](native-callbacks-benchmark.json)
record three alternating samples per mode with identical full libc and guest
programs. Each measured time includes loader/startup and excludes host rootfs
import. The host is shared x86_64 Linux executing the ARM64 runtime under QEMU;
these results do not establish physical-iPad or general application speedups.

The saved full-frame development binary already reuses immediate-return guest
instruction quanta. It transfers the whole 240-word sorting frame per comparison;
the current binary transports mutable header/path fields and rebuilds constants.
Neither workload uses experimental bsearch routing. The saved binary is an
unreleased development snapshot identified by its SHA-256, not a released commit.
This benchmark batch predates the subsequent shared-stack fallback fix and its
regression fixture. Its source commit and binary identities remain explicit;
the final fix is verified functionally without a second benchmark batch.

| Workload | Acceleration off | Current native | Previous full-frame native | Off/current ratio |
| --- | ---: | ---: | ---: | ---: |
| 2,048 integers, four qsort rounds | 3.6827 s | 3.9790 s | 3.4672 s | 0.93x |
| Unmodified Alpine sort -n, 2,048 rows | 17.7325 s | 18.1915 s | 18.6265 s | 0.97x |

Current integer sorting retires 5,475,837 guest instructions versus 11,836,440
with acceleration off, about 54% fewer. Recorded continuation transport falls
from 785,203,200 to 94,493,920 bytes; numeric sort falls from 170,146,560 to
20,389,160 bytes, about 88% in each case. The new counter also includes the small
kind-header probe, which the snapshot's whole-frame counter did not include.
Reduced bytes and guest instructions have not established a wall-time gain.
Samples vary substantially: the previous full-frame integer samples span
3.13–7.10 seconds. The measured regressions remain visible in the table.
The existing `context_switches` metric counts CPU slice entries, including
immediate native returns; it does not count outer task/signal sweeps.

The native body still validates/transports progress at every guest comparison.
BusyBox's comparator and atof/strtod remain guest execution. Next candidates are
numeric parsing, fewer boundary operations and larger cooperative arrays; byte
counts alone cannot justify calling an optimization faster.

The [bsearch diagnostic](native-bsearch-diagnostic.json) includes its complete
small C source, compile command, identical binary identities, exact invocations
and metrics. It is one pair rather than a stable speed estimate. Both modes have
pure functions enabled; only `--native-bsearch` differs. Confirmed native entry
overhead is the reason to retain original default routing while testing nested
search support explicitly.

## CI tracking

[Linux](https://github.com/kb5000/trames/actions/runs/38295539118) and
[Apple](https://github.com/kb5000/trames/actions/runs/38295538847) passed on
attempt 1 for the final shared-stack fix. Linux includes signed Python and
GCC/Clang/Git/CMake guest workflows. Apple includes native Rust core/memory/callback
checks, device/simulator links and seven actual iPad simulator tests with zero
failures. The downloaded build ZIP digest/CRC,
[actual simulator summary](native-callbacks-simulator.json), and both bundled
libc hashes were verified against the checked full-musl artifact. Conclusions,
job steps and artifact identities are in the [accompanying JSON](native-callbacks.json).

The initial callback commit’s Apple workflow passed on attempt 2. Its first job
failed in the existing pinned NIOSSH OpenSSH test with
`Sent channel window adjust on channel in invalid state`, before callback core
or simulator tests ran. Only that failed job was retried without source changes;
the first failure remains part of the verification record.
