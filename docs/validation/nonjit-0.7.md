# Static interpreter refinement: 0.7.0 / build 8

Runtime source: [`35d83cd`](https://github.com/kb5000/trames/commit/35d83cd670f08c2a3f0152666930dab67f4c8f43),
validated on 2026-10-12 (Asia/Shanghai). Default backend remains `cached`, with
stable Rust and no JIT. This refines [0.6.0](nonjit-0.6.md), adds two CPU extension
families, and preserves the current Linux ABI and disk format.

## Changes

The 128 decoded page slots now use 64 two-way sets. Global code-epoch changes
trigger checked page-token validation, rather than discarding unrelated decoded
pages. Mapping replacement, execute permissions, stores into code, shared
address spaces and epoch wrap remain invalidation boundaries. Cache memory stays
bounded and fork starts with an empty derived cache.

Warmed flags-setting ADD/SUB followed by a conditional branch can execute through
one dispatch within a page. Register/flag results and both guest instruction
counts remain visible. A one-instruction slice executes only the arithmetic.
No pair fetches cold instructions or crosses a page. See [architecture](../architecture/nonjit.md).

The reference implementation adds all scalar and vector FRINT32Z/32X/64Z/64X
forms in FEAT_FRINTTS, including FPCR rounding, signed zero, integer range limits,
NaNs/infinities and FPSR flags. It also adds FEAT_LRCPC2 unscaled acquire/release
loads/stores, including signed byte/halfword/word loads and SP/ZR/register aliases.
All three backends publish the same `frint` and `ilrcpc` HWCAP features.
Ordered accesses retain natural alignment in the profile without LSE2/USCAT;
the QEMU max alignment-fault comparisons use cross-16-byte cases that fail in
both profiles. This does not implement a multicore weak-memory model.

The historical 418-encoding corpus now has 388 accepted encodings, two SVCs and
28 rejections in every backend (previously 384/two/32). [CSV](nonjit-0.7-cpu.csv)
and [source/profile metadata](nonjit-0.7-cpu.json) distinguish decode acceptance
from independent semantic evidence. Remaining omissions still include PAC/BTI
enforcement, MTE, SVE/SME, later crypto and selected optional extensions;
this is not exhaustive ISA conformance.

## Measured program performance

Three interleaved samples per workload, comparing the preserved **0.6.0 cached**
binary with **0.7.0 cached**, on the same x86_64 Linux host. The updated harness
explicitly selects `--baseline-backend cached`. Each sample restores identical
signed-package files, history, pycache and metadata; Vim uses its normal defaults.

| Workload | 0.6.0 median | 0.7.0 median | Ratio |
| --- | ---: | ---: | ---: |
| Vim: spawn to first visible cursor | 1.939 s | 1.780 s | 1.090× |
| Python imports + SQLite/JSON/hash + exit | 7.446 s | 7.383 s | 1.009× |
| GCC -O2 compile, link, run | 2.618 s | 2.541 s | 1.030× |

Ranges: Vim 1.939–1.944 / 1.768–1.820 seconds;
Python 7.307–8.056 / 7.241–7.488; GCC 2.590–2.773 / 2.530–2.597.
Python's overlapping ranges and CPU-slice medians (6.930 / 6.991 seconds) show
no clear speed improvement; the elapsed-time ratio alone is not a meaningful win.
These three-sample shared-host results do not establish physical iPad performance.
The earlier 0.5/0.6 measurements are retained separately and are not combined
across batches to inflate this update's gain.

Median decoded-word counts fall from 11.44 to 2.70 million for Vim, 36.78 to
23.84 million for Python and 8.35 to 4.65 million for GCC. Vim page misses fall
from 230,481 to 50,809. CPU execution remains the dominant cost; this is not
evidence that the maximum non-JIT speed has been reached. Program outputs and
exit states are validated. The new advertised features slightly change Vim's
instruction count; Python's genuine random hash seed also changes counts.
[Raw samples](nonjit-0.7-benchmark.json) retain ordering, metrics and SHA-256s.

```sh
python3 tests/performance/nonjit.py \
  --baseline /path/to/preserved-0.6.0-trames-run --baseline-backend cached \
  --candidate target/release/trames-run \
  --vim-store /path/to/signed-vim-store --developer-store /path/to/m6-store \
  --samples 3 --output /tmp/nonjit-0.7.json
```

## Tail-call investigation

Stable Rust 1.90 rejects explicit `become` tail calls. Clang `musttail` works on
Linux and compiles for iOS arm64; a 10-million-step unoptimized prototype runs
with a 256 KiB stack. However, the synthetic tail dispatcher was slower than the
switch loop. It is retained as a reproducible research fixture, without changing
the production backend. [Design, limits and reproduction](../architecture/tail-dispatch.md)
and [raw synthetic samples](nonjit-0.7-tail.json) explain the result.

## Verification

Local fmt/clippy, 157 Rust tests, iOS target compilation and C++ ASan/UBSan passed.
All 14 cached-backend QEMU groups passed. New independent comparisons include
44,800 scalar/vector/FPSR FRINTTS snapshots, 960 LRCPC2 register/memory snapshots
and six ordered-access alignment faults. Regression tests cover both cache ways,
unrelated mapping changes, same-address remapping, execute-only pages, shared
code writes, generation/global epoch wrap, and fused arithmetic/branch conditions,
exact budgets, page faults and a branch replaced by SVC through another thread.
M6 event/IPC/lock/FIFO fixtures passed in all three backends. Reused-store cached
workflows also passed Git init/commit/merge/gc/HTTP clone, GCC/Clang compile/link
and QEMU execution, parallel Make, CMake/CTest, Python selectors/asyncio/threads/
SQLite/venv, adduser/su/htop, and persistence after reopening.

The Linux syscall inventory is unchanged: 318 entries, 133 implemented subsets,
183 missing and two obsolete stubs. All 185 undispatched numbers still return
ENOSYS in all three backends. [CSV](nonjit-0.7-syscalls.csv) and
[fingerprints](nonjit-0.7-syscalls.json) record this source revision.

[Complete Linux CI](https://github.com/kb5000/trames/actions/runs/38205290952)
passed on its first attempt for reference, cached and C++ backends. Every backend
runs all 14 QEMU groups, previous terminal/storage/network/TLS/Vim checks, signed
Python installation under 128 host descriptors, and fresh signed M6 developer
packages plus their actual workflows. [CI metadata](nonjit-0.7-ci.json) records
the exact source heads, all job steps and artifact digests.

## Apple build and installation

[Apple CI](https://github.com/kb5000/trames/actions/runs/38205290951) passed on its
first attempt: native OpenSSH PTY/SFTP, simulator/device builds and seven iPad
simulator tests, with zero failures or skips. The [actual simulator report](nonjit-0.7-simulator.json)
records iPad Pro 11-inch (M4), iOS 18.5 arm64. The intermittent NIOSSH dependency
issue [#9](https://github.com/kb5000/trames/issues/9) remains open; this release
does not claim to fix it.

[Download the unsigned device IPA artifact](https://github.com/kb5000/trames/actions/runs/38205290951/artifacts/11727840985).
Unzip on Windows and sign `Trames-unsigned.ipa` with Sideloadly using the same
Apple ID/effective bundle ID as the installed app. No data reset is needed.
Turn off the optional C++ switch to use the default Rust cached backend.
The artifact expires 2026-10-26 at 05:05:33 UTC.

The actual IPA is 9,414,647 bytes, SHA-256
`ec2fcd5a2f9579045dcd78750127e924104bbb87cdd10e3a86adc8196dc4d86e`.
Independent [verification](nonjit-0.7-ipa.json) checks its packaged checksum,
`org.trames.Trames`, version 0.7.0/build 8, executable permission, arm64 Mach-O
device platform, iOS 17 minimum and iPad device family. All 36 files match the
actual device-build app byte for byte. Artifact ZIP hashes also match GitHub's
published digests. Signing/install and performance of 0.7.0 on the physical
iPad remain owner acceptance work.
