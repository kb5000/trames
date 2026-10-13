# Native integer/string validation

Development source: `5dc763bda8033946d8460d2534363a7b78e5d6f3`;
implementation plus word-prefix and write-only-output corrections. No new device
release or signed IPA is published. Version 0.10.0/build 11 remains unchanged.
[Protocol, original-source reuse and memory contracts](../architecture/native-stdlib.md).

## Functional checks

- Strict Clippy and rustfmt passed; 211 workspace tests passed with network enabled
  for the existing socket tests. A restricted local invocation initially blocked
  two socket cases; the authorized rerun passed.
- 38 memory/native/accelerator tests passed on the actual ARM64 runtime under
  pinned QEMU 10.0.13. iOS core/distro/FFI compile-only checks passed.
- Original Alpine musl QEMU, ordinary QEMU with the complete replacement libc,
  and current Trames off/on on memory/disk stores produced the same 476 integer
  rows. Each row verifies all six signed/unsigned public conversions with exact
  result, errno and end position. Invalid bases, NULL for an invalid base,
  signed/unsigned extrema, negative unsigned conversion, hex/octal prefixes,
  malformed/high-byte input, guard pages, page splits and >64 KiB fallback passed.
- Six exported internal aliases, errno TLS in threads, shared user stacks,
  signals, fork and native qsort → guest comparator → native integer parsing passed.
- All seven string functions passed NUL/search/return-pointer/canary checks,
  bounded truncation and zero padding from zero through >64 KiB sizes. All 256
  bytes across eight alignments passed short-prefix checks. Guard pages, shared
  destinations and real `PROT_WRITE` destinations passed; unit checks also verify
  tagged pointers, preflight without effects and fork COW.
- The preceding published numeric host (`d976f34`) loaded the same new complete
  libc and passed the full fixture. Missing integer capability/string function
  bits selected original routines, without rejected or faulting native entries.
- The existing 712 floating rows passed all four off/on memory/disk modes without
  bit differences. All 16 complete-musl regression modes passed; 1,642 public
  exports remain, private helpers stay hidden. Loader upgrade, retained data,
  reopen, direct loader invocation and invalid-install rollback passed.
- Ten legacy static/dynamic/preload transport modes passed. Its provenance
  verifier now resolves both the existing flat and new original-directory layouts.
- Signed Python install/repair under 128 host descriptors and actual Python
  math/json/sqlite3/pty passed. Embedding PTY, resize, file roundtrip, lifecycle,
  cancellation and native Bridge checks passed.
- All 17 packaged guest/build inputs, ELF, original archive and complete GPL/MIT
  notices matched their declared hashes and repository bytes. The native intscan
  source differs only by its private symbol during compilation; the fallback
  intscan/strtol and original string sources remain pinned unchanged.

[Machine-readable local and CI evidence](native-stdlib.json) retains per-mode
metrics and exact source/runtime/fixture/libc identities. The existing numeric
input/replay/result-byte counters now cover shared integer/floating transport;
completed integer and floating conversion counts remain separate.

## Concentrated measurements

The optional `tests/performance/stdlib.py` uses three alternating samples for the
preceding published runtime with pure acceleration, current pure, and current off.
The baseline runtime was preserved before rebuilding and is fingerprinted. Each
mode uses the same complete libc and executable; application stores are freshly
copied from the same snapshots for every run, including pycache and Vim state.
No package installation or download is inside these timings.

The actual ARM64 runtime executes under QEMU on a shared x86_64 Linux worker.
Function-loop timings include guest loader execution but exclude host rootfs
import. Vim measures process spawn to first cursor visibility after normal startup,
with terminal query responses and subsequent ordinary `:qa!`. Python measures
process spawn through exit/final sync for unchanged imports, JSON, SQLite and SHA-256.
These are warm-host-cache observations, not physical iPad timings or a general
whole-system speedup. The raw report retains all samples, guest instructions,
native counts and runtime components; there are no timing assertions.

| Workload | Previous pure median | Current pure median | Current off median | Previous/current |
| --- | ---: | ---: | ---: | ---: |
| snprintf + strtoull, 4,096 conversions | 0.984 s | 0.950 s | 1.032 s | 1.036x |
| 8,191-byte stpcpy/strchr/strrchr, 256 loops | 0.812 s | 0.099 s | 1.083 s | 8.187x |
| Default Vim startup | 20.999 s | 23.412 s | 21.141 s | 0.897x |
| Python imports + JSON/SQLite/SHA-256 | 100.232 s | 101.217 s | 103.319 s | 0.990x |

[All raw samples and fingerprints](native-stdlib-benchmark.json). The integer
loop retires about 12.8% fewer guest instructions, but its measured gain is only
3.6%; entry/formatting cost limits the result. The long-string loop retires about
96.2% fewer guest instructions and measures 8.19x faster than the preceding pure
runtime (10.93x versus current off).

There is no established application startup gain. Vim's measured median is
11.5% slower, with overlapping ranges (previous 19.06–22.72 s; current 20.35–26.13 s).
Python's median is about 1.0% slower, also with overlapping ranges. Retain these
results rather than claim they are speedups or repeat timings until favorable.
Vim retires essentially the same instructions (89.88M versus 89.86M in sample 1);
Python rises slightly (320.97M to 321.36M). Neither application's measured startup
uses a completed native integer conversion. Vim still enters existing pure calls
about 39,800 times; Python about 19,400. The new coverage does not target most
startup work. Shared-worker/QEMU timing variation also limits causal conclusions.
Investigate existing boundary and checked-memory cost before adding more calls or
shipping a grouped device release; exact invalidation and permission checks remain
required. These measurements do not establish physical iPad regressions or gains.

## Linux and Apple CI

Final source `5dc763b` passed both workflows on attempt 1:

- [Linux runtime 38333554548](https://github.com/kb5000/trames/actions/runs/38333554548),
  job 115088488046: all CPU/ABI differences, complete musl, new stdlib fixture,
  network, signed package, Python, embedding and developer-tool steps passed.
- [iPad frontend 38333554315](https://github.com/kb5000/trames/actions/runs/38333554315):
  guest-musl job 115088486209 and apple-build job 115088726933 passed. Native Apple
  Rust/memory tests, SSH integration and simulator/device linking passed.
- Downloaded [actual simulator report](native-stdlib-simulator.json): **7 passed,
  0 failed**. The build ZIP digest and CRC were verified, rather than inferring
  success from a workflow step alone. Both bundled app libcs match the CI full-libc
  SHA-256 `ca4c92f5fe3cef2fcb86cd914d0319c2173e274c4d95a0c2f626e6c088775746`.
- The CI musl/source ZIP digest, embedded ELF, all 17 packaged inputs, pinned
  original archive and complete notices were verified against final source. Local
  and CI GNU compiler versions differ, so local/CI libc byte identity is not
  assumed; source inputs match and app bundles match their own CI libc.

Artifact IDs, exact ZIP digests, all step conclusions and per-mode runtime/native
metrics are retained in the [evidence JSON](native-stdlib.json). The unsigned
sideload artifact is a CI build, not a new release. Device signing/version updates
remain grouped to respect the owner's signing quota.

