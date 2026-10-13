# Node.js and interpreter memory follow-up

Runtime source: [`49ad262`](https://github.com/kb5000/trames/commit/49ad26209ae7b86cad01e6cfd2faaebbbcaeda30),
compared with baseline `63e219098fea285a64720a821970cf4e029d8526`.
The implementation is included in the 0.11.0/build 12 release batch. One cached interpreter, no host JIT.
[Implementation and limits](../architecture/nodejs-memory.md).

Both a fresh store and a reopened store pass signed Alpine Node.js 22.23.2 and npm 11.6.4 on
the development-host executor, using the complete hybrid musl at its standard
path. The fixture checks process.title, repeated V8 arithmetic, Intl/URL/JSON,
asynchronous files and crypto, compression, workers, child processes, localhost
lookup, HTTP server/fetch and virtual egress. Offline npm installs a local
dependency, writes a lockfile and loads it through ordinary CommonJS. A separate
runtime reopens the persisted Node/npm data. A separate fresh-store run also
passes with the original signed Alpine musl and acceleration disabled. CI runs
the hybrid-musl workflow on a fresh store.

The extended mmap fixture independently compares 4 GiB address reservation,
non-residency, permission SIGSEGV/SEGV_ACCERR, partial commits/removal, fixed
replacement and reserved mremap against QEMU. Actual ARM64 Trames execution
passes both memory and disk variants. Linux may apply a mapped prefix before a
failed mprotect across a hole; the comparison restores that prefix before reuse.
Trames preflights this case, keeping a failed commit atomic.

Rust tests cover failed memory/metadata-budget commits, fork COW, CLONE_VM,
range splitting and nonblocking standard-stream backpressure. ARM64 differentials
cover integer/load-store/structure operations and ordered accesses; the broader
existing musl and Linux regression suite runs in CI.

The manual performance harness is `tests/performance/cpu-memory.py`. It preserves
the preceding release binary and compares three alternating old/new ARM64 runs
under pinned QEMU. Its tight memory loop verifies its result and equal retirement;
ordinary Vim and Python start from fresh copies of identical stores. There is no
timing assertion, no benchmark in routine CI and no physical iPad timing claim.
The completed batch produced:

| Workload | Previous median | Current median | Ratio |
| --- | ---: | ---: | ---: |
| Fixed-size memory loop | 4.242 s | 3.315 s | 1.280× |
| Vim: first visible cursor | 21.813 s | 20.341 s | 1.072× |
| Python imports/SQLite/JSON/hash and exit | 107.300 s | 97.689 s | 1.098× |

All three tight-loop runs retire exactly 13,000,020 instructions in both versions.
Vim ranges are 20.867–26.648 / 19.830–20.721 seconds. Python ranges are
100.825–109.203 / 92.873–108.755 seconds: they overlap, and the third pair shows
little gain. The Python median is an observation, not a stable startup speedup.
Vim/Python retain the same program behavior; stack-string layout changes cause
small retirement differences (35/33 instructions), with an additional 270 in
one previous Vim PTY sample. No timing threshold decides correctness.
[Raw alternating samples and exact ELF/libc/store hashes](nodejs-memory-benchmark.json)
retain these limits; shared-host QEMU timings cannot predict physical iPad gain.

Local verification passes 218 workspace tests, 43 ARM64 memory/native/accelerator
tests, fmt, strict Clippy and iOS compilation. Independent ARM64 checks include
67 M1 comparisons, 615 structure load/store variants, 44,800 FRINTTS snapshots,
960 LRCPC2 snapshots and six alignment faults. Full-musl testing retains all
1,642 public exports and passes 16 memory/disk/native-on/off startup/runtime/
callback combinations; four stdlib combinations retain 476 exact integer rows.
The extended mmap/runtime-compat suites pass both storage modes on ARM64; the
virtual-network suite passes 27 QEMU comparisons and its DNS/ICMP/HTTPS checks.
Current inventories retain 877 decoded / 12 unsupported representative encodings
and 164 implemented syscall subsets / 154 undispatched ENOSYS probes. These
are audit counts, not universal ISA or Linux conformance percentages.

```sh
TRAMES_TEST_MUSL="$PWD/target/musl-hybrid/libc.so" python3 tests/integration/nodejs.py
python3 tests/performance/cpu-memory.py \
  --baseline /path/to/preserved/trames-run \
  --baseline-commit 63e219098fea285a64720a821970cf4e029d8526 \
  --vim-store /path/to/vim-store --python-store /path/to/python-store \
  --output /tmp/cpu-memory.json
```

Node/npm packages remain unmodified signed Alpine inputs; this is functional
coverage of the fixture, not universal npm-package or Node native-addon support.
Shared/file PROT_NONE, general demand paging, shared-proportional RSS, independent
virtual TCP keepalive retry timing and host JIT remain outside this change.

## Hosted source validation

[Linux CI](https://github.com/kb5000/trames/actions/runs/38346528037) and
[Apple CI](https://github.com/kb5000/trames/actions/runs/38346528032) pass on
49ad262, both on their first attempt. Downloaded simulator evidence reports
seven passed tests and zero failures; both app builds contain the exact musl ELF
from the paired source artifact. The original archive, 17 build/adaptation inputs,
embedded ELF, ZIP CRC/digests and GPL/musl notices were verified.
[CI jobs/steps](nodejs-memory-ci.json) · [Actual simulator report](nodejs-memory-simulator.json) ·
[ARM64 Node title/SHA256/Worker execution](nodejs-memory-arm-smoke.json).

The ARM64 Node smoke runs the actual ARM64 Trames binary under QEMU and retires
313,271,592 guest instructions, with zero rejected/faulted NativeCalls. The larger
Node/npm integration workflow runs on the development-host executor; this is not
a physical iPad claim. The subsequent version-only release build has its own
[0.11.0 artifact and validation record](release-0.11.md).
