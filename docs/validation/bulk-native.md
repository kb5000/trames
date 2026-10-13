# Direct backing / bulk native batch

Development after 0.13.0/build 14; follow-up [Issue #15](https://github.com/kb5000/trames/issues/15).
No new numbered IPA is delivered by this development batch.
[Memory/native contract](../architecture/bulk-native.md).

Local validation: 242 workspace tests, 187 selected ARM64 tests under QEMU,
plus the final native module rerun after the complete NEON extension (5 tests).
Clippy passes with warnings denied. Full upstream musl builds with the original
source archive and notices; adaptation count increases from 17 to 19.
Original musl and ordinary hybrid Linux/QEMU outputs match. Off/on, memory/disk
stdlib tests cover 476 integer rows, string concatenation/canaries/guards, bounds,
COW/shared VM, shared stacks/threads/signals/fork and nested qsort callbacks.
The saved 17-function host runs the new libc with original concat fallback.
Full hybrid integration also verifies public exports, callbacks, loader/TLS/threads,
file aliases/EOF/signals, code invalidation, retained user data and failed upgrade rollback.

The single before/after batch uses the same new musl and guest fixture on both
x86_64 host executors, one guest CPU, three alternating samples per workload.
The host interprets ARM64 guest instructions directly; NEON performance is not
measured here. Runtime includes guest startup and excludes rootfs import.
[Raw samples and binary/source fingerprints](bulk-native-benchmark.json).

| Workload | Before median | After median | Before/after |
| --- | ---: | ---: | ---: |
| 256 × 32-byte copy | 2.523 ms | 2.000 ms | 1.261× |
| 256 × 64-KiB copy | 2.742 ms | 2.927 ms | 0.937× |
| 16 × 1-MiB copy | 156.661 ms | 5.818 ms | 26.926× |
| 256 × 64-KiB shared fill | 7.117 ms | 6.414 ms | 1.110× |
| Unchanged BusyBox sort -n, 2,048 rows | 433.589 ms | 422.123 ms | 1.027× |

The large-copy improvement primarily removes the old >64-KiB fallback, rather
than demonstrating the same gain for previously native calls. Shared fill removes
16,777,216 buffered write bytes in this fixture and replaces them with direct
resident writes. The 64-KiB copy regresses about 6.7%; short millisecond workloads
and startup contribute noise. Small sort change is not proof of broader application
improvement. No iPad timing or whole-system speed claim.

Linux/Apple exact-source CI and actual simulator verification are pending publication.
