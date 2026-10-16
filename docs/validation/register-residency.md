# Decode dispatch and register residency (2026-10-16)

**No candidate is promoted.** Four guest values really can stay in host
registers, but this fixed-cache implementation loses substantially to its
nonresident parent. Decode-time dispatch alone is more promising: several
single-thread workloads improve, while Vim and four-thread medians worsen in
noisy samples. This is insufficient evidence for replacing the production path.
It does not establish a 2× gain or a physical iPad slowdown.

Production runtime source is restored to the baseline. Only two new semantic
CPU regression tests remain in the Rust diff. The three implementations are
archived as [independently applicable patches](register-residency/), not runtime
switches or maintained backends. [Raw measurements, hashes, assembly and logs](register-residency.json).

This investigation starts at `0ca2111c16d9dbde2fd94deab51c5bc5eed3da4f`.
It measures complete interpreters, using the same guest binaries and Release
toolchain as the production interpreter. No runtime code generation, executable
allocation, guest-specific fast path or additional shipping backend is introduced.

## Designs

- **special:** select a precompiled handler at decode time and dispatch through
  a constant function-pointer table. Fourteen additional W/X forms specialize
  ordinary-GPR ADD/SUB immediate, unshifted non-S ADD/SUB and AND/ORR/EOR, and
  MOVZ/MOVN. Other forms keep the original semantics. Width and handler identity
  share one byte, retaining 32-byte instructions and 40-byte generation entries.
- **resident:** build on `special` with four explicit scalar tail arguments.
  Guest x0-x3 persist through integer, branch and decoded load/store handlers.
  Complete reference fallbacks publish/reload them; slice/page/syscall/native,
  decode-fault and execution-fault exits publish them. This is a fixed ABI
  register cache, not block-local liveness analysis or register allocation.
- **table:** isolate decode-time handler selection without the fourteen extra
  operand specializations or register cache. It keeps the original 26 handlers.

The register experiment is materially different from the preceding
[integer pairs](integer-blocks.md): it carries four independent values across
arbitrarily many tail transitions, including decoded guest memory operations,
rather than forwarding only the previous instruction's result.

## ARM64 evidence

The resident ADD-immediate handler really carries guest x0-x3 in host x24-x27;
its hot tail transition does not write these values back to the guest register
array. This is confirmed from generated AArch64 instructions, not inferred from
Rust local variables. Normal function calls inside memory/fallback handlers can
still require saved values; it is not a promise of zero spills everywhere.

The obstacle is operand routing: choosing a cached value by a dynamic guest
register number adds a decision tree to each read and write. The representative
64-bit ADD-immediate handler grows from 220 bytes in `special` to 508 bytes in
`resident`, despite both being frameless. The corresponding original handler
is 404 bytes. Thus successful physical residency does not establish a speedup.
Static code size is evidence of code generation, not a cycle measurement.

The table reduces dispatch code replicated in every handler. On the same ARM64
build the original NOP handler is 336 bytes; `special` is 180 bytes. Its
precompiled handler address is loaded from a trusted constant table, and the
transition is a single indirect branch. Guest memory cannot supply host targets.

## Correctness and memory model

All candidates retain exact budgets, PC/fault publication, SP versus XZR, W-write
zero-extension, lazy flags, executable-page epochs and generation-wrap clearing.
Only reached instructions are fetched. No guest memory permission, host pointer,
atomic, synchronization or weak-memory rule changes. There are no new global
locks, and no promises to make thread-unsafe guest operations safe.

Handler identities are private decode products, refreshed when flags/branch
fusion changes an entry. The constant TAG must correspond to that exact immutable
entry; this preserves the existing executor's unchecked variant invariant.
Specialized GPR indexing additionally requires decode-proven indices below 31.
The resident version uses the ordinary register accessor for nonresident values.

## Measurement method

All comparisons alternate binaries with fixed CPU affinity. Each application
gets a fresh clone of the same prepared store. CoreMark is upstream default
`PORT_DIR=linux`, -O2, 10,000 iterations, with valid CRCs and >=10 seconds per
scored sample. The long memory loop checks its checksum and equal retired guest
instruction counts. Python/Vim/GCC use the same hybrid musl, pure routing and
native allocator routing off. No builds or regression runs overlap timed batches.

The available timing host is shared x86_64, not the shipping ARM64 iPad. ARM64
QEMU runs establish execution correctness, not physical device speed. Small
ratios near one are not decisive, and results from separate batches should not
be compared directly. All raw samples, including unfavorable ones, are retained.

The baseline is built and preserved before source edits. A repeated no-change
Cargo invocation yields the same hash; this is an incremental-build check, not
an independently clean reproducible-build claim. The restored host and ARM64
Release binaries are byte-identical to their respective preserved baselines.
Binary/source hashes and exact compiler identity accompany the data.

## Results and decision

`special` screens at 1.023× CoreMark, 1.068× memory, 1.006× Vim and 0.959×
Python. These are two alternating pairs per workload; they do not establish a
general benefit from the fourteen extra forms. The next comparison uses that
same `special` binary as its baseline to isolate four-value residency:

| Workload | Pairs | Special median (s) | Resident median (s) | Special / resident |
| --- | ---: | ---: | ---: | ---: |
| coremark | 2 | 27.175 | 50.239 | 0.541× |
| cpu-memory | 2 | 1.958 | 2.465 | 0.794× |
| python-imports | 2 | 12.159 | 16.371 | 0.743× |

Residency works functionally, but extra operand-routing branches, state
publication and larger handlers accompany it. The timings measure their combined
cost; assembly identifies the routing tree without isolating its cycle share.

The final `table` comparison restores the original operand semantics and uses
an unchanged production binary as baseline. Single-thread workloads use CPU 1;
four-vCPU work uses CPUs 0–3 and 30 million iterations per guest thread.

| Workload | Pairs | Production median (s) | Table median (s) | Production / table |
| --- | ---: | ---: | ---: | ---: |
| coremark | 3 | 32.123 | 31.175 | 1.030× |
| cpu-memory | 3 | 2.389 | 2.147 | 1.113× |
| vim-default | 3 | 2.245 | 2.385 | 0.941× |
| python-imports | 3 | 9.887 | 8.936 | 1.106× |
| gcc-build-run | 3 | 3.094 | 2.657 | 1.164× |
| smp-work | 3 | 7.603 | 8.447 | 0.900× |

Ratios below one mean longer elapsed time. The table's CoreMark and memory
candidate is faster in each paired sample. Vim is nearly equal in two pairs and
has one much slower candidate sample. Four-thread paired ratios range from
0.891× to 1.229×, with mixed signs, despite the aggregate median ratio of 0.900×.
The production CoreMark baseline itself ranges from 31.695 to 45.716 seconds.
All samples are retained. These variations prevent precise portable speedup
claims; the decision is to defer promotion, not to declare the table inherently
slower. No additional timing batches are selected to obtain a preferred result.

## Validation and reproduction

- `special`: 43 host CPU tests, 507 independent warmed integer snapshots and
  22,272 NZCV sequences on the host; host and ARM64 Release builds.
- `resident`: 45 host CPU tests, 507 snapshots on each host and ARM64/QEMU runner,
  22,272 host NZCV sequences, and both Release builds.
- `table`: 353 workspace tests and 45 ARM64 CPU tests; formatting and strict
  workspace/all-target/all-feature Clippy; 507 integer snapshots and 22,272 NZCV
  sequences on each host and ARM64/QEMU runner; base/M1 differentials, actual
  musl fault/signal restoration, 1/2/4-vCPU SMP, mmap/EOF/COW/code aliases and all
  19 native-parallel entries. iOS CPU Release code generation and core/distro/FFI
  cross-check pass. These checks do not claim SDK linkage or device testing.
- Restored production: 353 workspace tests, 45 ARM64 CPU tests, formatting,
  strict Clippy, host/ARM64 Release builds and 507 independent warmed snapshots
  on each runner pass. The only Rust diff is two tests inside `cfg(test)`.

The retained tests exercise 720 generated width/operand cases twice (cold
and warm), and 104 exact-budget/final-instruction combinations spanning scalar
and paired memory, reference fallback, yield, syscall and fault exits. The
independent single-step interpreter supplies expected register, NZCV and memory
state. Existing cache invalidation, generation, branch fusion and small-stack
regressions remain in force.

All three patches independently pass `git apply --check --cached` against the
pinned baseline. Reproduction example:

```sh
git worktree add --detach /tmp/trames-register-lab 0ca2111c16d9dbde2fd94deab51c5bc5eed3da4f
git -C /tmp/trames-register-lab apply /absolute/path/to/docs/validation/register-residency/resident.patch
cd /tmp/trames-register-lab
cargo test -p trames-cpu --locked
cargo build -p trames-cli --release --locked
TRAMES_RUNNER="$PWD/target/release/trames-run" python3 tests/differential/integer_blocks.py
```

Use `tests/performance/execution.py` with preserved Release binaries and the
same prepared app store, alternating order, fixed affinity, three samples and
10,000 CoreMark iterations. The resident screen's baseline is `special`, whereas
all other comparisons use the production baseline. These are distinct questions;
do not combine medians across batches. No version bump or IPA is requested by
this investigation.

## What this implies for micro-op fusion

Fixed register caching alone leaves dynamic operand routing in the hot path.
A more ambitious design would assign a few physical slots at block preparation
and choose precompiled handlers with those slot operands already fixed. It
would also fold constants and remove dead intermediate writes. Interpreting
each small IR operation separately can erase that saving, so dispatch must be
measured along with the arithmetic and register traffic.

This investigation does not implement an SSA allocator, constant-folding fusion,
or memory-containing superinstructions, and it does not disprove their possible
benefit. They require exact reconstruction at partial budgets and faulting memory
operations. A future experiment should measure fixed-slot routing against this
four-value dynamic-routing result before multiplying handler combinations.
