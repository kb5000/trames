# Direct backing / bulk native batch

Development after 0.13.0/build 14; follow-up [Issue #15](https://github.com/kb5000/trames/issues/15).
No new numbered IPA is delivered by this development batch.
[Memory/native contract](../architecture/bulk-native.md).

Local final validation: 244 workspace tests and 189 selected ARM64 tests under QEMU.
Clippy passes with warnings denied. Full upstream musl builds with the original
source archive and notices; adaptation count increases from 17 to 19.
Original musl and ordinary hybrid Linux/QEMU outputs match. Off/on, memory/disk
stdlib tests cover 476 integer rows, string concatenation/canaries/guards, bounds,
COW/shared VM, shared stacks/threads/signals/fork and nested qsort callbacks.
The saved 17-function host runs the new libc with original concat fallback.
Full hybrid integration also verifies public exports, callbacks, loader/TLS/threads,
file aliases/EOF/signals, direct-write disk sync/unmap/reopen, code invalidation, retained user data and failed upgrade rollback.

The initial before/after batch measured source `deeb56d3b52f22216a691df6b3655a853f0e0898`,
before the reduced-fallback follow-up below. Its raw fingerprints remain unchanged.
No extra timing batch was run for the subsequent policy changes.
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

## Reduced-fallback follow-up

One-shot output validation permits resident shared, undetached private-file and
write-only targets without relaxing resumable callback state. Integer/floating
result frames use it as well. Pure string scans/copies now accept 1 MiB; numeric
parsing/callback limits stay 64 KiB. Large bounds can stop early for forward
comparison/search and strncat. Fixed bulk copies, strncpy padding and reverse
search beyond the cap still retain original fallback before effects.

The expanded final fixture (including SIZE_MAX strncat) has 5,825 native calls and
6 fallbacks in both memory/disk enabled modes; all 19 per-function pure fallback
counters are zero. Remaining six are integer scans exceeding the parsing ceiling.
The saved 17-function host on the identical new libc/fixture has 5,780 native calls
and 169 fallbacks. [Final stdlib metrics](bulk-native-stdlib.json),
[initial fixture metrics](bulk-native-stdlib-initial.json),
[stdlib transcript](bulk-native-stdlib.txt) and [full musl transcript](bulk-native-hybrid.txt)
preserve coverage and boundaries; different fixture revisions are not mixed.
The memory fault no-effect assertion uses the address space's own revision,
avoiding unrelated parallel tests advancing the process-wide backing counter.

Linux/Apple final exact-source CI and actual simulator verification are pending publication.
