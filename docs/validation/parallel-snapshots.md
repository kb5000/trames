# Incremental parallel snapshots and configurable memory

2026-10-16 development follow-up, based on `a5e8868`. There is no version bump
or new IPA in this batch. [Raw data and source/compiler/binary hashes](parallel-snapshots.json)
and [validation logs](parallel-snapshots/) accompany this report.

## Observed problem and fix

Actual ARM64/musl Node 24.18.1 running the official npm code-server 4.141.0
exposed a large preparation cost. A four-vCPU `--version` run spent **183.854 s**
in parallel memory preparation/writeback out of **217.218 s** runtime.
The later counter diagnostic confirmed **1,932 full snapshot rebuilds** and
**172,427,235 page records traversed**, versus only **139,320 source pages read**.
The diagnostic overlapped another compatibility run and compilation; its
311-second wall time is retained, not used as a controlled speedup result.

Small mapping/protection changes and executable-page publication advanced the
address-space code revision. The existing cache then rebuilt its complete
immutable frame/page tables. Node repeatedly allocates/protects small ranges in
an address space averaging roughly 89,000 committed pages during those rebuilds.
Ordinary data writes already had a dirty-frame path; mapping/code changes did not.

The fix records affected virtual pages and reservation changes, maintains the
joined snapshot in place under safe unique Arc ownership, and updates root-frame
positions, alias counts and executable-page classification incrementally.
Executable worker writeback records its changed frame metadata too. A global
backing epoch checks only indexed backed pages, preserving actual backing
version/EOF checks. Workers release views before sending completion; externally
retained views retain the immutable rebuild path. No permission, COW, backing
writeback, atomic/exclusive or region-lock contract is weakened.

## Four-vCPU observation after the fix

Same ARM64 Node/npm package and memory budgets, all four guest CPUs available,
no taskset pinning, no host JIT and no code-server-specific runtime path:

| Measurement | Initial observation | Fixed run |
| --- | ---: | ---: |
| Runtime seconds | 217.218 | 29.986 |
| Parallel memory seconds | 183.854 | 2.297 |
| CPU execution seconds | 27.058 | 23.335 |
| Peak running virtual CPUs | 4 | 4 |

The fixed counter result has **one** full rebuild (19,318 initial page records)
and **1,804** incremental updates touching **138,831** page records. Source page
reads remain similar at 137,425: the gain comes mainly from removing unrelated
metadata rebuilding, not changing the program's reads or instructions.
Peak host RSS for this version probe is 1,138,264 KiB, separate from guest budgets.

These are individual observations on a shared x86_64 Linux development host,
not a frequency-controlled multi-sample benchmark or physical ARM64/iPad result.
The initial diagnostic enabled syscall tracing; the fixed run did not. Retain
that distinction instead of claiming a universal 7× gain. The independent
unmodified host Node comparison is 0.467 s; the same ARM64 Node/package under
QEMU takes 6.254 s. These different execution environments are not iPad timings.

An affinity-limited run also completed version output in 30.951 s before the
fix. Pinning helped diagnose the problem; production remains genuinely multicore.
CLI `--metrics` now exposes rebuild/update/read counts and prints diagnostics on
runtime failure as well as normal exit.

## Memory configuration

[Runtime, CLI, JSON ABI v3 and iPad settings](../guides/guest-memory.md) now accept
total and per-address-space committed-page budgets, preserving 1 GiB / 512 MiB
defaults. Positive aligned values and process <= total are validated before
starting the guest; CLI rejects invalid options before creating a store.
Fork charges distinct spaces, CLONE_VM charges once, and exec receives the
remaining aggregate/per-process budget. sysinfo, proc meminfo and memory-related
getrlimit report the configured policy. Large values do not preallocate RAM or
raise the operating system's actual app memory allowance.

## Validation

- **360 workspace tests**, strict formatting and Clippy, iOS ARM64 core/distro/FFI
  compile checks.
- **142 ARM64/QEMU tests** for memory, CPU and core.
- **78 TSan tests**, with the Rust standard library instrumented, for memory and
  native acceleration.
- New structural regressions exercise repeated reservation/protection/unmap and
  root-slot reuse over 1,024 unchanged pages: one full rebuild, 128 changed-page
  updates. Additional cases cover retained views, shared aliases, private COW,
  backing epochs and worker-written executable metadata.
- Actual static musl memory-budget fixture under **1 and 4 CPUs** checks sysinfo,
  proc and rlimits, process allocation failure, shared pthread accounting,
  aggregate fork failure and exec. Invalid configurations fail before disk writes.
- Independent QEMU comparisons pass mmap/EOF/truncate SIGBUS, COW/discard/remap,
  executable aliases; SMP 1/2/4 CPUs including DMB, futex races, fork and exec;
  actual musl signal handlers/context recovery; all **19 native entries**,
  conversions and nested callbacks, with actual four-way native overlap.
- Real embedding ABI test starts Alpine with 64 MiB total / 32 MiB per-process
  and observes those settings from the PTY, retaining its file/bridge/lifecycle
  regression. Swift: **57 tests, 0 failures, 1 skipped SSH integration test**
  (no OpenSSH test peer in that invocation). App Swift syntax parses locally;
  full SwiftUI/platform checks require Apple CI.

## code-server status

See [inputs and manual probe](../guides/code-server.md). The official package,
signed Node 24 APK and original-source ARM64/musl addons are used, with a minimal
opt-in handshake-timeout patch. Unmodified code-server's ten-second parent/child
handshake actually timed out; the patch leaves its default unchanged and accepts
a longer explicit test timeout without altering guest clocks.

HTTP `/healthz` succeeds. The first affinity-limited browser attempt exceeded
its 180-second navigation timeout while VS Code initialized. It did not establish
workbench/WebSocket, editing, terminal or extension acceptance. The zero-MAC
message comes from VS Code's optional machine-ID lookup and falls back internally;
it is not proof that HTTP transport is broken. Inotify remains unimplemented.
The subsequent unrestricted four-vCPU run serves HTTP and renders the actual
Monaco workbench/Welcome page (title `Welcome - project - code-server`), with five
WebSocket attempts. It still logs repeated **remote ExtensionHost authentication
handshake timeouts**, shell-environment startup timeout and actual `fs.watch`
ENOSYS errors. Editing, saving and terminal acceptance remain unverified.
The screenshot step timed out waiting for fonts after DOM capture; no screenshot
is claimed. A concurrent identity-encoding diagnostic was interrupted by probe
cleanup, so it does not establish gzip as a bottleneck. Browser JSON and server
logs preserve those limits. HTTP or DOM creation alone is not full compatibility.
