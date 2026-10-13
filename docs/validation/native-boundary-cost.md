# Entry and checked-memory validation

Development source `dc1fb217055da77b10c2beeea89b6944470fb910`, following published
`c45c8b9`. The native profiles and complete guest musl are unchanged; no new
signed device release is published. [Implementation and unsafe boundary policy](../architecture/native-boundary-cost.md).

## Functional evidence

- 214 workspace tests, strict Clippy and rustfmt passed. 41 memory/native/accelerator
  tests passed on actual ARM64 under pinned QEMU 10.0.13. Core/distro/FFI iOS
  compile checks passed.
- Cache adversaries exercised valid hits, unrelated data/mapping changes, fork
  identities, execute permission removal, MAP_FIXED replacement, cross-page RET
  mutation after warming the current writable-executable mapping, shared file
  alias writes and live EOF failure without relying on a version change.
- Owned C-string results retain bounds/NUL, non-UTF-8 bytes, tags, guard termination,
  cross-page reads, CLONE_VM permissions, live backing updates and private copy
  detachment. Native fill/copy tests retain COW, alias coherence, zero length,
  complete cross-page preflight and executable-page tokens.
- All 16 full-musl modes and loader upgrade/reopen/rollback checks passed; 1,642
  public exports remain. The 712 floating and 476 integer rows passed all four
  off/on memory/disk modes. Real enabled stdlib runs assert cache hits and misses.
- Ten legacy static/dynamic/preload modes passed, including precise faults and
  executable mutation. Actual shared/private mmap, truncate/EOF SIGBUS, remap,
  seals, code aliases and file APIs passed on memory and disk.
- Signed Python install/repair under 128 host descriptors and Python execution
  passed. Embedding PTY, resize, disk roundtrip, Bridge, lifecycle and cancellation
  passed.
- Owner-directed policy now allows audited production unsafe boundaries, requiring
  explicit operations through unsafe_op_in_unsafe_fn deny. The isolated optional
  allocator probe also passed strict Clippy; it is not a runtime dependency.

[Machine-readable source, binary and CI evidence](native-boundary-cost.json).

## Concentrated before/after batch

The baseline CLI was preserved from `c45c8b9` before edits. The allocation probe
uses identical source, manifest and lockfile against the old/current dependency
implementations in separate build trees. Each process warms the reached operations,
then runs 20,000 iterations. GlobalAlloc transparently counts System allocation
and reallocation requests; counts exclude warmup and report output. Probe timings
include validation/assertions and counter overhead, so they do not predict an
application's allocator cost directly.

The application batch alternates preceding/current pure runtimes three times with
identical libc, executable and freshly copied stores. Python uses the same fixed
PYTHONHASHSEED=0 in both modes. C-loop retirement is checked equal across modes;
application entropy, clock queries and PTY delivery can still alter retirement.
Vim measures spawn to first visible cursor with normal configuration, terminal
responses and subsequent :qa!. Python measures spawn through successful exit and
final sync for ordinary imports, JSON, SQLite and SHA-256. Guest C-loop timings
include the loader and exclude rootfs import. All runs execute the actual ARM64
runtime under QEMU on a shared x86_64 worker, with warm host caches; these are not
physical iPad timing or whole-system speedup claims.

| Probe, 20,000 operations | Old/new allocation requests | Old/new requested bytes | Median old/current |
| --- | ---: | ---: | ---: |
| path-resident | 40,000 / 20,000 | 41,620,000 / 660,000 | 1.800x |
| path-split | 80,000 / 40,000 | 82,900,000 / 980,000 | 1.468x |
| empty-resident | 20,000 / 0 | 81,920,000 / 0 | 9.047x |
| fill-512 | 0 / 0 | 0 / 0 | 1.390x |
| native-fill-512 | 0 / 0 | 0 / 0 | 1.341x |

Requested-byte totals count repeated allocation/reallocation traffic, not peak
resident memory. Resident/split path allocation counts halve; empty resident
strings allocate zero times. No allocation was added to fill or native entry.
Probe timing includes the counting atomics/assertions and is specific to the
isolated process.

| Guest workload | Previous median | Current median | Previous/current |
| --- | ---: | ---: | ---: |
| snprintf + strtoull, 4,096 conversions | 1.121 s | 0.955 s | 1.173x |
| 8,191-byte string loops, 256 iterations | 0.100 s | 0.096 s | 1.048x |
| Default Vim startup | 22.606 s | 22.358 s | 1.011x |
| Python imports + JSON/SQLite/SHA-256 | 104.011 s | 104.556 s | 0.995x |

[All samples and input fingerprints](native-boundary-cost-benchmark.json).
Guest instruction counts matched exactly between modes in all observed samples,
including the applications. The integer and long-string loops measure about
17.3% and 4.8% higher throughput. Vim is about 1.1% faster by median; Python is
about 0.5% slower. These application changes overlap shared-worker variation and
do not establish a material startup improvement. Preserve the full result rather
than repeat until favorable.

In application sample 1, Vim reuses 39,848 validations with only 9 word-comparison
misses; Python reuses 19,469 with 10 misses. Every invocation still checks the
reached executable page token/permissions/EOF. Signature reads and path allocation
traffic have decreased, but almost all startup work remains interpreted CPU
execution (89.86M and 321.42M retired instructions). Further application gains
need execution/memory profiling; these results do not justify a JIT or another
backend, and do not predict physical iPad gain/regression.

## CI verification

Final source dc1fb21 passed both workflows on attempt 2:

- [Linux runtime 38339472276](https://github.com/kb5000/trames/actions/runs/38339472276),
  final job 115109258154: CPU/ABI differences, complete musl, cache assertions,
  native/virtual network, signed package/Python/embedding and developer workflows
  passed.
- [iPad frontend 38339472266](https://github.com/kb5000/trames/actions/runs/38339472266),
  final apple-build job 115109246098 and guest-musl job 115109276018: native Apple
  tests, OpenSSH, simulator/device linking and simulator tests passed.
- Downloaded [actual simulator report](native-boundary-cost-simulator.json):
  **7 passed, 0 failed**. ZIP digest/CRC and both app-bundled libc hashes were checked
  against CI full-libc SHA-256
  ca4c92f5fe3cef2fcb86cd914d0319c2173e274c4d95a0c2f626e6c088775746.
- CI full-musl ZIP, embedded ELF, all 17 source inputs, pinned original archive and
  complete GPL/MIT notices were verified. Its libc matches the preceding stdlib
  build: this round changes host runtime and policy, not guest library bytes.

The initial Linux job 115106988454 failed before DNS comparison because its test
peer could not bind 127.0.0.1:53 (EADDRINUSE). The initial Apple job 115107197883
failed in the previously recorded pinned NIOSSH closing-state window-adjust fatal
error before native core/app tests. Only the failed jobs were retried once with
identical source; the original failures remain in the evidence JSON. Subsequent
network/SSH checks and all remaining steps passed. No timing or correctness check
was removed to obtain a passing result.

Artifact IDs/digests, all job steps and exact source/binary identities are in the
[verification JSON](native-boundary-cost.json). The generated unsigned IPA is a CI
artifact; no new device release or signing/version update was published.

