# Native CoreMark iPad probe

Temporary app probe, opened from `+` → `原生 CoreMark`. The normal Linux data
store and bundle identifier remain the same. Run in the foreground, with Low
Power Mode disabled and the device cooled down. Copy/share the original output.
The app pauses local Linux and prevents automatic screen locking during the run.
An inactive/background transition invalidates the app's acceptance of the score.

The five algorithm/framework `.c` files and `coremark.h` are byte-identical to
EEMBC/CoreMark commit `1f483d5b8316753a742cbf5590caf5bd0a4e4777` (Apache-2.0).
The original POSIX port/header are retained. **One port line changes**:
`CLOCK_REALTIME` → `CLOCK_MONOTONIC`, avoiding wall-clock adjustments. Original
hashes and paths are recorded in `upstream.json`; the original license is here
and in the app's Notices. App integration/overrides are GPL-3.0-or-later.

Settings: `-O2 -fno-lto`, single thread, 2000 total data bytes, heap, argv seeds
`0 0 0x66` (performance) or `0x3415 0x3415 0x66` (validation). No algorithm
substitutions, LTO, PGO, special `march`, or guest accelerator calls. Upstream
auto-calibration chooses iterations with a >=10 second scoring target; total
elapsed time may exceed a minute. The original CRCs and time validity checks
determine acceptance. A short simulator smoke run deliberately fails the time
requirement and is never presented as a score.

Compare native `Iterations/Sec` with the same iPad guest's default
`make PORT_DIR=linux` output. These are throughput measurements; native Apple
Clang and guest GCC still differ. They do not isolate interpreter-only overhead
or represent an officially certified/published EEMBC result.

CoreMark's `main` is renamed at compilation to `trames_coremark_main`, and
`HAS_PRINTF=0` routes only CoreMark's report into a bounded buffer, leaving app
stdout/stderr intact. The upstream global timer is protected against overlapping
probe runs; this lock has no relationship to guest/native-call synchronization.
The C benchmark executes on a user-initiated worker, not the main/UI thread.
