# Non-JIT acceleration: 0.6.0 / build 7

Validated on 2026-10-12 (Asia/Shanghai) at runtime commit
[`bebc59f`](https://github.com/kb5000/trames/commit/bebc59f9f35d2a129ae60708e08ac26e88dcf917).
The default local backend is `cached`: bounded Rust interpretation with lazily
decoded, generation-tagged code pages. No executable-memory allocation or JIT
activation is used. CPU capabilities and Linux ABI scope remain those of
[0.4.0 CPU](../roadmap/compatibility-0.4.md) and [M6](../roadmap/compatibility-m6.md).
[Architecture](../architecture/nonjit.md) describes cache invalidation and limits.

## Real-program comparison

Three interleaved samples per program/backend on the same x86_64 Linux host,
comparing the preserved 0.5.0 reference binary with 0.6.0 cached interpretation.
Every run restores the same signed-package fixture, including Vim history,
pycache and file metadata. Vim retains its normal configuration and plugins.
No minimal-startup flags or guest-program replacements are used.

| Workload | 0.5.0 median | 0.6.0 median | Speedup |
| --- | ---: | ---: | ---: |
| Default Vim: spawn to first visible cursor | 5.708 s | 2.162 s | 2.64× |
| Python imports, SQLite/JSON/hash checks, exit | 24.832 s | 7.435 s | 3.34× |
| GCC -O2 compile, link and execute C | 8.307 s | 2.515 s | 3.30× |

Baseline/candidate ranges: Vim 5.693–5.782 / 1.982–2.240 seconds;
Python 23.419–26.485 / 7.367–7.610; GCC 8.137–8.477 / 2.504–2.683.
These are shared-host, warm-cache measurements with three samples, not a
statistical guarantee or physical iPad measurements. The Python case is a broad
standard-library import workload, rather than empty interpreter startup.

Vim executes exactly 88,688,447 guest instructions in every final run; GCC's
workload executes exactly 127,283,245. Python's genuine entropy-backed hash seed
causes small count differences; program inputs and validated results are identical.
Installed inputs: Alpine 3.22.6, musl 1.2.5-r12, Vim 9.1.1566-r0,
Python 3.12.15-r0 and GCC 14.2.0-r6. Versions were read from guest package databases.

[Raw samples](nonjit-0.6-benchmark.json) record binary/manifest/source SHA-256,
host, ordering, instruction counts and elapsed-time breakdowns. Measured candidate
binary SHA-256:
`de91b08adc9bda52c5a0140044b0544af2b58787d70d0a77929dc0c2dbea30a1`.

Profiling the initial block-cache prototype found about 23.9 million Vim block
lookups and 2.8 million block reconstructions. Page-based reuse reduced the final
workload to about 5.1 million decoded-page lookups, with only two complete proc
snapshots (initialization and actual proc access). CPU slice elapsed time remains
about 2.04 seconds of a 2.26-second runtime. Interpretation and operand/dispatch
costs remain the main measured path; this does not prove a non-JIT maximum.
Further static decoding, cache policy, block linking/superinstructions and
storage indexing can be evaluated separately.

After preparing signed-package stores and preserving the baseline binary:

```sh
python3 tests/performance/nonjit.py \
  --baseline /path/to/preserved-0.5.0-trames-run \
  --candidate target/release/trames-run \
  --vim-store /path/to/signed-vim-store \
  --developer-store /path/to/m6-developer-store \
  --samples 3 --output /tmp/nonjit-comparison.json
```

`--include-reference` adds the candidate reference backend for attribution.
Application timing is manual, has no CI threshold and is not rerun for every
small edit. Program exit timings include loading and final sync. Runtime metrics
separately time CPU slices, syscalls and idle periods with host monotonic elapsed
time, including host scheduling.

## Correctness and workflows

Local fmt/clippy, all 153 Rust tests and iOS target compilation passed. New checks
include 5,120 randomized/reserved words against the single-step oracle, exact
budgets/SVC, warmed self-modifying ordinary/atomic stores, DC ZVA, permissions,
independent/shared spaces, epoch wrap, bounded cache reuse and misaligned targets
inside warmed pages. Fixed-size operands match generic accesses across faults
and page boundaries; frame tests cover COW, slot reuse and rights. A guest with
no proc access verifies virtual-device initialization. Proc probes cover symlink
aliases, cwd/dirfd and missing process directories.

[Full Linux CI](https://github.com/kb5000/trames/actions/runs/38198782497) passed
independently for reference, cached Rust and C++ at this commit. Each runs all
13 QEMU differential groups, C++ ASan/UBSan, previous BusyBox/PTY/storage/network/
TLS/Vim checks, signed Python installation under 128 host descriptors, and fresh
signed M6 developer packages and real workflows. Locally the final cached backend
also passed all 13 groups, the M6 ABI fixture in all three modes, and reused-store
Git/GCC/Clang/Make/CMake/CTest/Python/adduser/su/htop and virtual HTTP Git workflows.
Fresh package installation is established by CI.

The Linux v6.12 inventory remains 318 entries: 133 implemented subsets,
183 missing and two obsolete stubs. All 185 undispatched numbers return ENOSYS
in all three backends. [CSV](nonjit-0.6-syscalls.csv) and
[source fingerprints](nonjit-0.6-syscalls.json) describe this revision;
historical M6 evidence is retained. Entry presence is not full ABI conformance.
[CI metadata](nonjit-0.6-ci.json) records source heads, job steps and artifact digests.

## Apple build and installation

[Apple CI](https://github.com/kb5000/trames/actions/runs/38198782482) passed on its
first attempt: native OpenSSH PTY/SFTP, simulator/device builds and seven iPad
simulator tests, with zero failures or skips. The [actual report](nonjit-0.6-simulator.json)
records iPad Pro 11-inch (M4), iOS 18.5 arm64. The intermittent NIOSSH dependency
issue [#9](https://github.com/kb5000/trames/issues/9) remains open; it did not occur
in this run. Physical acceptance remains in [#8](https://github.com/kb5000/trames/issues/8).

[Download 0.6.0/build 7 unsigned IPA](https://github.com/kb5000/trames/actions/runs/38198782482/artifacts/11723974238).
Unzip and sign `Trames-unsigned.ipa` using Windows Sideloadly with the same Apple
ID and effective signed bundle ID to update the existing app. Current v3 Linux
data is retained; no reset or JIT activation is required. Keep the experimental
C++ switch off to use the new default Rust interpreter.

Downloaded archive digests match GitHub metadata; the packaged IPA checksum also
matches. It is an executable arm64 iPhoneOS device Mach-O, supports iPadOS 17+,
retains `org.trames.Trames`, and all 36 app files agree byte-for-byte with the
device build. IPA size: 9,409,356 bytes; SHA-256:
`6cd5633db6ef00b346e0a7fa308b73a64425110d2c91d91371a808f5cdab7277`.
[Verification metadata](nonjit-0.6-ipa.json) records details. IPA expiry:
2026-10-26 03:23:55 UTC; build reports: 2026-10-19 03:23:58 UTC.

For device startup timing, run `vim --startuptime /tmp/vim-startup.log`, quit
normally and inspect that log. Signing/physical installation remain owner work;
simulator acceptance does not establish behavior or performance on their iPad.
