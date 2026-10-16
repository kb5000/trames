# Integer blocks without JIT (2026-10-16)

**Do not promote these candidates.** The final pure-pair implementation does not
show a general speedup on the available development host: CoreMark takes about
9% longer, Vim/Python about 5% longer, and the four-thread compute workload about
31% longer. The memory workload is effectively unchanged. This investigation
does not establish a 2× gain, or any physical iPad performance result.

Production Rust sources are restored to the original interpreter. The repository
retains nine independently applicable implementation patches, measurements,
code-generation evidence and a new independent warmed-integer regression test.
There is no experimental backend switch in the shipping runtime.

The baseline is `67c26c2f648545f8daa94e8eb638a5b80d72c01b`: the production
preserve-none tail interpreter and permission-tag TLB, compiled in Release with
nightly-2026-10-16 / LLVM 23.1.4. This investigation changes execution structure,
not the guest program, CoreMark sources, native allocator routing or memory
permissions. It does not introduce executable mappings or runtime machine code.

## Implemented designs

All candidates are complete interpreters with bounded caches, ordinary memory
and reference fallbacks, exact instruction budgets and native/syscall exits.
The [patches](integer-blocks/) apply independently to the pinned baseline; they
are experiment snapshots, not separately maintained production backends.

1. **Integer loop:** collect up to 16 previously decoded integer operations;
   execute them with one budget/PC boundary. A precomputed operand map forwards
   the preceding result, and suppresses a write immediately overwritten by the
   next instruction. The first prototype forms blocks immediately; `hot-block`
   waits for repeated code-page visits.
2. **Threaded integer blocks:** specialize each block operation with the same
   explicit tail ABI as the ordinary interpreter. `compact` separates block
   exit handling and removes bounds checks whose limits follow from immutable
   cache construction. `pending` records reached block boundaries instead of
   scanning all 1,024 entries when a page becomes hot.
3. **Static pairs:** 169 precompiled combinations of 13 integer operation
   classes. The two operations share their Rust implementation with the normal
   interpreter; a local intermediate can cross the operation boundary without
   an intervening dispatch. ADR/ADRP are converted to a correctly sized constant
   at preparation. `pairs` still has profiling and an outlined finish helper;
   `inline` removes the production counters, inlines finish and delays formation
   to the 64th page visit. `candidate` separates partial budgets and narrows the
   optional branch to B.cond. `pure-pairs` finally removes that optional branch
   and preserves the existing ADD/SUB-flags-plus-B.cond fusion as its own boundary.

The final pure-pair design stores at most 256 pairs per code page, inside the
existing 128-page cache. Pending boundaries use bounded 16-bit slots and reuse
allocation across page replacement. Runtime preparation produces data only;
all operation handlers were compiled into the application beforehand.

## Correctness boundaries

- Only instructions actually fetched at reached PCs participate. No lookahead
  fetch, inaccessible-page probe, speculative guest fault or cross-page pair.
- SP and XZR are separate dependency identities. W writes zero-extend before
  forwarding; MOVK/BFM old destinations and multiply accumulators participate.
- A one-instruction remainder takes a separate handler and publishes that first
  result, including a write otherwise eliminated by the second operation.
- No pair contains memory, a syscall, native entry, trap or unsupported operation.
  Architectural state is published before these operations. Guest memory
  permissions, atomic operations and SMP synchronization are unchanged.
- Data-only register forwarding does not make guest functions thread-safe or
  serialize workers. Each CPU owns its cache and transient integer result.
- Code-page identity/generation invalidation clears prepared pairs. Interior
  rewrites, execute revocation, generation wrap, independent address spaces,
  interior branch targets and precise fault PCs remain observable correctly.
- Unsafe indexing relies on private constructors, immutable prepared pairs and
  bounded dispatch indices. Guest addresses never become host handler pointers.

The ordinary and fused paths share scalar semantics. The independent single-step
interpreter remains an oracle; a second production backend is not introduced.

## What the experiments exposed

The generic block loop was slower on CoreMark and Python. Replacing it with tail
handlers and reducing preparation work helped, but did not establish a general
speedup. Hot-path instrumentation itself added two counter updates per block;
final candidates retain those counters only in unit-test builds.

Inlining is also insufficient by itself. The optional B.cond path pulled its
condition validation and carry checks into every pair specialization. On ARM64,
the representative ADD-immediate pair in `candidate` was 1,544 bytes and acquired
a 16-byte entry frame. In `pure-pairs`, it is 604 bytes and frameless; the existing
single ADD handler is 404 bytes. These are static code-generation observations,
not cycle counts or a measured speedup. Partial-budget handling is separate and
152 bytes for this operation. All assembly and binary fingerprints are in the
[data](integer-blocks.json).

The early instrumented loop covered approximately 23% of retired CoreMark guest
instructions and 13% of Python instructions. This is an instruction-count share,
not a measured CPU-time share, and cannot establish an Amdahl-law speedup bound.
It does show that an integer-only design leaves substantial execution untouched.

## Measurements

Every batch alternates baseline/candidate on identical guest inputs, the same
compiler, fixed CPU affinity and fresh cloned application stores. No builds or
regression suites run concurrently with timing. CoreMark uses unchanged default
`PORT_DIR=linux` code, 10,000 iterations, validates its CRCs and exceeds ten seconds
in every measured run. The long memory fixture validates a checksum and an equal
retired-instruction count. Vim measures first visible cursor; Python imports
modules; GCC compiles, links and executes a program. All application runs use the
same complete hybrid musl with pure routing and native allocator routing off.

The shared x86 host is particularly noisy in this session. The unchanged baseline
memory workload ranges from 2.00 to 7.82 seconds within the earlier three-sample
batch; the threaded screen has a 60.91-second baseline CoreMark sample, compared
with roughly 26–28 seconds in other batches. Neither apparent speedup is reliable
in isolation. Earlier screens use CPU 0; the final pure-pair batch pins both
binaries to CPU 1. Results from different batches must not be compared directly.

| Workload | Complete pairs | Baseline median (s) | Pure-pair median (s) | Baseline / candidate |
| --- | ---: | ---: | ---: | ---: |
| coremark | 2 | 25.123 | 27.407 | 0.917× |
| cpu-memory | 3 | 2.013 | 2.002 | 1.005× |
| vim-default | 3 | 2.034 | 2.142 | 0.950× |
| python-imports | 3 | 8.981 | 9.460 | 0.949× |
| gcc-build-run | 3 | 3.059 | 3.144 | 0.973× |
| smp-work | 3 | 2.281 | 2.977 | 0.766× |

The cloud execution connection restarted during the third CoreMark repetition.
The two complete alternating pairs are retained verbatim; no third result is
invented and the interrupted file is labeled explicitly. The remaining app
workloads were completed in a fresh batch on the same fixed CPU. Four-thread
compute uses CPUs 0–3 and 30 million iterations per thread. Ratios below 1 mean
the candidate took longer. These observations support rejecting promotion, not
precise claims about the size of a physical-device slowdown.


All initial screens, unfavorable samples and exact CPU-affinity lists are retained
in [integer-blocks.json](integer-blocks.json). `final-single` in that data is the
older inline/optional-branch candidate, not the final pure-pair candidate. The
first immediate-loop screen has binary fingerprints and samples but no exact
source patch; all archived patches are separately checked against the baseline.
QEMU checks execute ARM64 code, but do not measure physical iPad performance.

## Validation and reproduction

- Final pure-pair snapshot: **48 host CPU tests**, including every template,
  budget/interior-entry checks, register roles, faults, capacity and invalidation;
  **507 independent warmed snapshots on each of the host and ARM64/QEMU runners**;
  host and ARM64 Release builds.
- The preceding optional-branch candidate additionally passed **356 workspace
  tests, 48 ARM64 CPU tests, 22,272 independent NZCV sequences**, base/M1
  differentials, musl signal restoration, 1/2/4-vCPU SMP, mmap/EOF/COW/code aliases,
  and all 19 parallel native entries. Strict Clippy and iOS compilation passed.
  Exact per-variant logs are retained; these totals are not attributed to an
  untested binary.
- All **nine patches** pass `git apply --check --cached` independently against
  the recorded baseline.
- Restored production: **351 workspace tests**, strict workspace/all-target/all-feature
  Clippy, formatting, **507 warmed snapshots on each host and ARM64/QEMU runner**,
  host/ARM64 Release builds and iOS CPU code generation/core-distro-FFI checks pass.
  Tracked runtime sources match the baseline. Rebuilt binaries are recorded
  separately: they are not byte-identical to the preserved baseline artifacts,
  despite matching runtime source and compiler, so this report makes no binary
  reproducibility or precisely isolated small-speedup claim.


The new `tests/differential/integer_blocks.py` deliberately calls a helper on a
different code page 128 times per pair. This heats the actual pair path regardless
of scheduler slice size, then compares nine GPR results and NZCV with independent
AArch64 QEMU. Its 507 snapshots cover all 169 operation-class combinations with
three edge seeds. This test is also added to ARM64 Linux CI.

Use an isolated checkout of the exact baseline to apply any one archived patch:

```sh
git worktree add --detach /tmp/trames-integer-lab 67c26c2f648545f8daa94e8eb638a5b80d72c01b
git -C /tmp/trames-integer-lab apply /absolute/path/to/docs/validation/integer-blocks/pure-pairs.patch
cd /tmp/trames-integer-lab
cargo test -p trames-cpu --locked
cargo build -p trames-cli --release --locked
TRAMES_RUNNER="$PWD/target/release/trames-run" python3 tests/differential/integer_blocks.py
```

Use `tests/performance/execution.py` with preserved Release binaries and a prepared
application store to repeat the alternating comparison. The fixture and store
hashes, compiler identity, individual timings, runtime counters, source hashes and
validation output are recorded in the JSON. No IPA or version bump is part of
this investigation.

## Next investigation

The result disproves the assumption that batching a few integer operations is
inherently faster than the existing tail interpreter. Preparation, metadata
loads, operand-mode branches, increased code footprint and register publication
still have costs. Their individual contribution needs profiling; elapsed-time
comparisons alone do not isolate each one.

A next experiment should measure physical ARM64 dispatch/register traffic, then
try a smaller set of fully specialized operand forms (width, shift, flags and
SP/XZR roles known at decode time) before expanding block length or template
count. Larger blocks containing memory would also require preserving exact
fault boundaries and existing memory semantics. They are not implemented or
promised to double performance in this report.
