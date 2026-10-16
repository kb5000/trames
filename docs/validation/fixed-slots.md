# Fixed operand slots and micro-op folding (2026-10-16)

This experiment follows [register residency](register-residency.md) and starts at
`2d99da2b5d0bf9003f6aeed44ceb5531f3aacfae`. Four block-local values are routed to
precompiled handlers at preparation time, removing the earlier per-instruction
dynamic register-selection tree. Constants, adjacent immediate additions and
dead intermediate writes can also collapse into fewer operations. There is no
runtime machine-code generation or executable allocation.

**No candidate is promoted.** Tiny ARM64 micro-handlers work, but complete
workloads show mixed performance and remaining regressions. Production runtime
source is restored, with the useful regression tests retained.
The implementations are available as [patches](fixed-slots/) against that exact
baseline; [raw measurements and fingerprints](fixed-slots.json) accompany this report.

## Implemented design

Each decoded code page may prepare up to 128 pure integer blocks, each at most
16 guest instructions long. Preparation waits for 64 page visits and examines
only already reached, decoded words. All four slots are assigned from the
register identities actually used by each block, including SP or ZR. This is a
fixed four-identity mapping, not SSA allocation with slot recycling or spilling.
A fifth distinct identity ends the block. Memory, branches, flag producers,
reference fallbacks, native calls and other potentially faulting operations also
end the block. Existing flag/branch pairs remain on the ordinary executor.

The builder supports MOVZ/MOVN/MOVK, register MOV, ADR/ADRP, non-S immediate
ADD/SUB, LSL forms of non-S ADD/SUB and logical operations, logical immediates,
UBFM/SBFM/BFM and ordinary MADD/MSUB/MUL/MNEG, with W/X and SP/ZR semantics.
It tracks constants, combines consecutive same-width immediate additions to the
same destination, drops pure ZR writes, and eliminates overwritten values that
are not read. Every modified architectural register remains live at full exit.
It loads only values read before definition and publishes dirty values once.

Each immutable micro-op contains parameters and a trusted precompiled handler
address. Operand slots, operation and width are const generic parameters of that
handler. Four explicit scalar tail arguments carry the values; handler transitions
need no dynamic register-number lookup, CPU PC update, budget check or page check.
A final sentinel publishes the results. The ordinary 26 interpreter handlers keep
their original three-argument ABI; entering/exiting a micro-op chain is a normal
call boundary. All this is a derived execution cache, not a second shipping CPU.

Two admission policies are tested: `prototype` permits two-instruction blocks;
`min4` requires four. The latter is the only performance refinement, motivated by
separate executed-block histograms. Its tests include rejection of shorter blocks.
Neither policy is chosen from guest identity or benchmark names.

## Correctness boundaries

Before entry, the entire original instruction count must fit the remaining slice
budget. Otherwise the original first instruction executes and later entries retain
their ordinary semantics. Thus partial budgets, interior entry and intermediate
writes remain observable. Full execution retires the original count regardless of
folding and advances PC once. Pending NZCV is unchanged by these non-S operations.
No observer, memory fault or native callback occurs inside an admitted block.

Page invalidation and generation wrap discard derived blocks along with decoded
entries. Fork starts a new cache. The new raw micro-op pointer is confined to an
immutable boxed slice: each real handler advances exactly one element, the builder
always appends a finish sentinel, and finish returns without advancing. Guest code
cannot provide this pointer or handler targets. Existing memory permissions,
address translation, atomics and weak-memory behavior are unchanged; no locks or
invented guest race errors are introduced.

## ARM64 code generation

The initial ARM64 Linux Release build contains 2,696 specialized micro-handlers.
For representative fixed operands, ADD-immediate is **4 host instructions**,
ADD with shifted source is **5**, and MADD is **3**. For example:

```asm
; fixed::step::<19, true, 0, 1, 2, 3>
madd x22, x24, x23, x25
ldr  x0, [x21, #32]!
br   x0
```

The four values occupy x22-x25; state and the micro-op pointer use x20/x21. These
handlers have no stack frame, architectural-register array accesses or dynamic
slot-selection branches. This demonstrates actual generated machine code, rather
than assuming that Rust locals stay in registers.

The surrounding `tail_fixed<Memory>` is 1,912 bytes, including entry, exit,
partial-budget fallback and next-operation dispatch. That is static size, not the
number of instructions executed on every entry. Its normal call and state
loading/publication still cost work. The complete host binary grows from
5,251,104 to 5,750,352 bytes; ARM64 grows from 5,058,736 to 5,605,720. Those are
whole ELF sizes, including symbols, not an instruction-cache footprint estimate.
Neither assembly nor QEMU timings establish physical iPad performance.

## Separate coverage diagnostic

A temporary instrumentation patch adds relaxed process-global counters and prints
cumulative histograms at cache destruction. It is never present in a timed binary.
CoreMark uses 1,000 iterations for this diagnostic only; this is not a scored
CoreMark run. The counters cover the whole process, including startup.

| Workload | Guest instructions | Full block calls | Average block length | Guest instructions in blocks | Block instructions removed |
| --- | ---: | ---: | ---: | ---: | ---: |
| CoreMark | 296,684,277 | 19,597,979 | 2.947 | 19.46% | 3.89% |
| Python imports | 321,409,614 | 13,079,595 | 2.302 | 9.37% | 7.14% |
| Memory loop | 130,000,020 | 0 | — | 0% | — |

About 78.5% of CoreMark block calls and 94.9% of Python block calls have only two
or three instructions. Python prepares 228,214 blocks over cache replacements;
CoreMark prepares 413. The memory loop has no consecutive eligible integer region
in its hot body, so its three prepared blocks receive no full-block calls. Any
small timing change there cannot be credited to micro-op execution.

Even a dramatic speedup *inside* these blocks has limited whole-program coverage.
The profile does not isolate cycle shares for preparation, calls, publication,
code layout or dispatch. It supports testing a larger admission threshold, not
claiming that one particular cost explains the entire regression.

## Measurement method and results

The timing host is shared x86_64 Linux. Both runner binaries execute the same ARM64
guest programs with the same pinned nightly Rust Release profile (opt-level 3,
ThinLTO, overflow checks enabled). Single-thread comparisons use CPU 1; four-vCPU
work uses CPUs 0-3. Binaries alternate order and application stores are freshly
cloned per sample. Guest programs are unchanged. CoreMark uses the upstream
`PORT_DIR=linux` -O2 configuration, 10,000 iterations, valid CRCs and at least ten
seconds per scored sample. Applications use the same hybrid musl in pure routing
with allocator routing disabled. Static fixtures and CoreMark use their original
libc. No build, profiling or regression processes overlap the timed batches.

The first two-pair screen gives production/prototype ratios of **0.898× CoreMark**,
**0.986× memory** and **0.958× Python**. A ratio below one means longer elapsed time.
Python baseline samples range from 8.927 to 10.636 seconds, so its small aggregate
change is not a precise effect estimate. All samples are retained.

The four-instruction policy receives three alternating pairs per workload:

| Workload | Production median (s) | Min4 median (s) | Production / min4 |
| --- | ---: | ---: | ---: |
| coremark | 27.030 | 26.562 | 1.018× |
| cpu-memory | 2.159 | 2.128 | 1.015× |
| vim-default | 2.030 | 1.926 | 1.054× |
| python-imports | 8.292 | 8.889 | 0.933× |
| gcc-build-run | 2.511 | 2.671 | 0.940× |
| smp-work | 2.290 | 2.151 | 1.065× |

The remaining regressions prevent promotion. These are whole-workload medians,
not portable speedup guarantees. The paired CoreMark ratios are 1.095×, 1.034×
and 0.995×. GCC is faster in one pair and slower in two; Vim is faster in two
and slower in one. SMP's final production sample is 3.785 seconds compared with
2.132 and 2.290 seconds in its other samples, so its aggregate 1.065× ratio is
particularly sensitive to host noise. No samples are dropped or repeated to
obtain a preferred result. The small positive memory result measures layout and
other surrounding costs: the coverage diagnostic records no executed blocks in
that workload.

**Neither candidate is promoted.** The production runtime is restored byte for
byte at source level. The new independent snapshots and stronger warmed small-stack
regression remain, with the snapshots added to Linux CI. The final host and ARM64
Release binaries are compared with their preserved baselines after restoration;
the outcome and hashes are recorded in the raw report.

This establishes that static operand routing can produce very small ARM64 handlers,
while this bounded, pure-integer-block implementation fails to deliver stable
whole-program gains. It does not establish a 2× improvement, a physical iPad
slowdown, or that every form of micro-op fusion is ineffective. Longer regions,
slot reuse and precisely faulting memory operations remain future designs. A next
experiment should first increase useful coverage and reduce entry/publication
boundaries, then measure on physical ARM64 before expanding handler combinations.
No additional implementation variants or timing batches are added this round.

## Validation and reproduction

- The two-instruction prototype passes 49 host CPU tests, strict CPU Clippy,
  507 warmed integer-pair snapshots and 22,272 NZCV sequences. The new 264
  independently generated region snapshots pass on the host and ARM64/QEMU
  runner, and on the original production host runner to check the harness.
- `min4` passes **357 workspace tests and 49 ARM64 CPU tests**, formatting,
  strict workspace/all-target/all-feature Clippy, host and ARM64 Release builds,
  iOS CPU Release code generation and core/distro/FFI cross-checks. These are
  compile checks without SDK linkage or device testing.
- `min4` passes 264 new region snapshots, 507 existing integer snapshots and
  22,272 NZCV sequences on each runner; base/M1 differentials; real musl fault
  handlers; 1/2/4-vCPU SMP including DMB forbidden-outcome tests; memory/disk mmap,
  EOF/COW/code aliases; and all 19 native-parallel entries.
- The four prototype-only Rust tests verify 7,296 operation/width/overlap snapshots,
  all budgets and interior entries for a folded/dead-write region, SP/ZR and mixed
  widths, state publication before fault, code rewrites, generation wrap, execute
  permission revocation, four-slot and metadata limits, and short-block rejection.
- The retained small-stack test executes six million warmed tail transitions on
  a 256 KiB host stack. The retained independent snapshots run 128 repetitions
  with a cross-page call each time, exercising the real page-visit threshold
  rather than only forcing cache preparation from a unit test.
- Restored production passes **353 workspace tests and 45 ARM64 CPU tests**,
  formatting, strict workspace Clippy and both Release builds. The 264 new region
  snapshots pass on both restored runners. Both final Release binaries are
  **byte-identical** to their respective preserved production baselines.

Both runtime patches independently apply to the pinned baseline, and every
included source/test file is checked against its recorded SHA256 after application.
`instrumentation.patch` applies on top of `prototype.patch` only. It is diagnostic
code, not a runtime feature or a benchmark binary.

```sh
git worktree add --detach /tmp/trames-fixed-lab 2d99da2b5d0bf9003f6aeed44ceb5531f3aacfae
git -C /tmp/trames-fixed-lab apply /absolute/path/to/docs/validation/fixed-slots/min4.patch
cd /tmp/trames-fixed-lab
cargo test -p trames-cpu --locked
cargo build -p trames-cli --release --locked
TRAMES_RUNNER="$PWD/target/release/trames-run" python3 tests/differential/fixed_slots.py
```

Use `tests/performance/execution.py` with preserved baseline/candidate binaries,
the same prepared store and long memory fixture, three alternating pairs, 10,000
CoreMark iterations and 30 million SMP iterations per thread. The raw reports
record exact fixture/library/binary hashes, CPU affinity, compiler identity,
individual timings, CPU time and runtime counters. Do not combine medians from
different batches or interpret ARM64/QEMU validation as device timing.
