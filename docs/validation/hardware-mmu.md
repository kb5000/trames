# Hardware MMU and direct-mapped window cache

The ordinary RAM path now uses hardware-protected CPU aliases, native inline
operands and the same physical bytes as native functions. See the
[architecture](../architecture/hardware-mmu.md) for ownership, host-page grouping,
permission/fault rules, sparse addresses and preserved ARM concurrency semantics.
One non-JIT interpreter remains.

**Current implementation:** [CPU aliases in the 256-entry page TLB](mmu-page-tlb.md),
source `7c96434`. That report contains current tests, code generation, all paired
samples and the explicit absence of a demonstrated general application speedup.
The extra resident-hint tail arguments described below are historical experiments,
removed from production. This report retains every earlier comparison, including
regressions, rather than replacing unfavorable results.

## Resident-hint validation and code generation

Local checks: **375 workspace tests**, strict workspace/all-target/all-feature
Clippy, and **108 ARM64/QEMU CPU/memory tests** pass. Memory tests include actual
host access faults for every operand width, simulated 16 KiB mixed-permission
subpages, COW/remap/discard, cached collisions/absence and stale owner generations in that earlier API,
retained worker snapshots and native/CPU coherence. Isolated subprocesses check
unrelated default host faults and previous handlers' queryable masks/NODEFER/one-shot rules.
The default child explicitly restores SIG_DFL so a sanitizer's crash policy does
not change that assertion. Linux-user children re-exec through QEMU when binfmt
is unavailable. Signal forwarding now explicitly reconstructs the previous handler mask from the
interrupted context, including when an interposed dispatcher supplies another
mask. Default redelivery first unblocks its signal. The host memory suite passes
these paths; the complete instrumented-standard-library TSan batch passes 88
tests on production source 97092c9, including the final default-redelivery adjustment. TSan does not instrument
raw asm bytes; its result checks the Rust-side region/ownership protocol, not
every native memory instruction.

Real guest mmap/EOF/SIGBUS/COW/code aliases, edited musl signal contexts,
same-page 1/2/4-CPU regions, 507 independent warmed integer snapshots and all 19
native pthread entries/nested callbacks pass. These broader workflows were run on
the first inline candidate; the refined fallback additionally passes the full
workspace and ARM CPU/memory suite. Exact-commit CI covers the final source.

The final iOS ARM64 Release core compiles to assembly on Linux, including its real
worker interpreter instantiation. [Final selected handlers](hardware-mmu/arm64-final.s)
show the hint owner/descriptor/base carried in the preserve-none tail ABI
(x24/x25/x26 in this build). Ordinary ADD-immediate does not spill these values.
The complex memory handler still spills/reloads them while decoding widths and
handling slow paths: **this is not a globally reserved, spill-free base register**.
Its native operands are inline, with no `read_array_hint` ABI call on the hit path.
Neither compile-only evidence nor QEMU timings establish physical iPad speed or
SDK/device linkage. Apple CI must execute the real Darwin remap/fault ABI.

## First complete comparison

Baseline: `538232a9243b605e11afae7fe8d87f23c4bf1116` (same runtime as the prior
CoreMark measurement source). Both binaries use pinned nightly-2026-10-16,
optimized Release/thin LTO/overflow checks. [Source and binary hashes](hardware-mmu/first-source.json)
and an independently applicable [first-candidate patch](hardware-mmu/first-candidate.patch)
retain the measured version before the fallback refinement.

The unchanged general [execution harness](../../tests/performance/execution.py)
alternates three pairs per workload, with identical guest ELFs/libc and fresh
copies of the same prepared Alpine store. Single-thread tests use host CPU 1;
four-vCPU work uses CPUs 0–3. CoreMark is upstream linux-port GCC -O2 with 10,000
iterations, valid CRCs and every scored sample >=10s. The memory fixture runs
10 million iterations with equal checksum/retired instruction counts. SMP uses
30 million iterations per thread. Apps use pure hybrid musl, allocator routing off.
No other builds or regression runs overlap the timed batches.

This is a shared **x86_64 development host**, with frequency/load not controlled;
small ratios and three samples do not establish portable gains. Raw favorable
and unfavorable samples are retained in [host data](hardware-mmu/first-host.json)
and [SMP data](hardware-mmu/first-smp.json). Ratios are baseline/candidate elapsed
time; below one means the candidate is slower.

| Workload | Baseline/candidate median ratio |
| --- | ---: |
| CoreMark | 1.047× |
| memory loop | 1.285× |
| Vim first cursor | 1.092× |
| Python imports | 0.828× |
| GCC compile/link/run | 0.940× |
| four-vCPU fixed work | 0.874× |

The first version improves the memory loop but regresses Python and some other
workloads. Inspection identifies duplicate hardware attempts on fallback and
uncached missing windows. The retained refinement removes those costs and adds a
regression for mapping after a cached miss. A second complete three-pair batch is retained in
[refined host data](hardware-mmu/refined-host.json) and
[refined SMP data](hardware-mmu/refined-smp.json): CoreMark 1.063×, memory 1.089×,
Vim 0.926×, Python 0.848×, GCC 0.717×, SMP 0.933×. This does **not** establish a
general application improvement. Python/GCC retired instruction counts are equal;
the added time is mainly CPU execution, rather than snapshot preparation.
Different batches are separate comparisons on a shared host; do not combine their
medians to claim an isolated refinement gain.

Assembly inspection also identifies runtime division by the host-page granule.
The final implementation stores its power-of-two shift at construction and uses
shifts for hot group indexing. The final assembly has zero UDIVs in the selected scalar handler, compared with
ten before this change. This is code-generation evidence, not a cycle attribution.
All earlier unfavorable measurements remain. Physical iPad MMU speed is unmeasured.

Initial exact-source CI caught signal-composition issues: Linux/TSan deferred
signal masks, and Darwin one-shot-handler default redelivery. These failures are
recorded rather than claimed as passing pipelines. A new early native Apple
memory-ABI job precedes the app build, while full Linux/Apple validation is rerun.

## Resident-hint comparison after shift indexing

The final resident-hint source is `97092c9`; 65a43ec/d516f84 change tests/docs only. Current page-TLB source 7c96434 is measured separately.
[Provenance and source/binary hashes](hardware-mmu/provenance.json),
[all final host samples](hardware-mmu/final-host.json) and
[all final SMP samples](hardware-mmu/final-smp.json) retain this third and last
resident-hint batch. The baseline/candidate store manifest hash matches both previous batches.
The same three-pair method and fixed-work inputs apply. The later page-TLB architecture receives its own complete batch; none of these
samples are removed or combined across batches.

| Workload | Baseline median (s) | Final median (s) | Baseline/final |
| --- | ---: | ---: | ---: |
| coremark | 30.220 | 27.987 | 1.080× |
| cpu-memory | 2.260 | 2.085 | 1.084× |
| vim-default | 2.982 | 3.114 | 0.958× |
| python-imports | 10.909 | 14.700 | 0.742× |
| gcc-build-run | 3.045 | 3.103 | 0.981× |
| smp-work | 2.321 | 5.191 | 0.447× |

The hardware path and direct-mapped cache are implemented and correctness-tested;
**general acceleration is not demonstrated by these development-host results**.
CoreMark and the memory loop have modest median gains, while app/SMP observations
include substantial regressions and variability. SMP candidate samples are
5.191/5.951/2.509s against baseline 2.299/2.321/2.366s. Those slow samples also
consume more child CPU time; snapshot setup remains a small part of elapsed time.
Do not describe this as only startup/mapping overhead or discard the slow samples.

The fixed-work SMP loop is mostly ALU/branch instructions. Extra live tail arguments,
register pressure, code layout/prediction and host scheduling need profiling;
these comparisons do not isolate their contributions. This led to the separately
measured [page-TLB implementation](mmu-page-tlb.md), which removes resident hints.
No benchmark-specific runtime branch or extra interpreter backend is added.

Native [Apple ARM64 memory ABI tests](hardware-mmu/apple-memory-abi.log) pass all
59 library + 4 translation cases on d516f84, including real 16KiB Mach remaps and
fault recovery. The [d516f84 Apple pipeline](https://github.com/kb5000/trames/actions/runs/38929761386)
passes all jobs, including native core suites, SDK builds and simulator acceptance.
Its [Linux pipeline](https://github.com/kb5000/trames/actions/runs/38929761456)
is still running later integration workloads at this update. Later page-TLB source
has its own native Apple validation and CI links in the current report.

## Vulkan cold-start regression

The same round fixes an independently reported startup error. With GPU enabled,
FFI startup imported the same ICD library and JSON as new files on every boot.
The second cold process failed with `virtual filesystem errno 17` (EEXIST).
[Before](hardware-mmu/reopen-before.log) and [after](hardware-mmu/reopen-after.log)
logs use the actual app C ABI. The old library SHA256 was
`c6da5dcebb35f4b64b600c0c7f9c4534b1793bd3319e002b9c46192289b064fc`.

The distro installer now validates the bundle before namespace changes, reuses
identical inodes, and replaces updated files through synced sibling imports and
atomic rename. Missing manifests are repaired; existing user files and symlink
targets are preserved. Ordinary VFS exclusive-creation behavior remains intact.
The [regression](../../tests/integration/embedding-reopen.py) passes first/second,
updated-bundle, GPU-disabled and GPU-enabled-again cold processes with exact ICD
hashes and persistent user data. It is now in Linux CI after ICD fixture creation.
No data reset is needed; disabling the GPU option is the temporary workaround for
already installed affected builds.

### Darwin query limitation

Independent native subprocesses reveal a real public-API limit: the original
one-shot handler terminates on the second signal, but native `sigaction` query
then restore loses that behavior on Darwin. XNU's getter reconstructs other flags
but omits `ps_sigreset` / SA_RESETHAND. Automatic chaining cannot recover a flag
that the query omits; this integration limit is explicit in the architecture.
The final regression compares forwarding against an independent native
query/restore and also runs the untouched native control. It does not claim
preservation of an unobservable pre-existing Darwin flag. The untouched native control proves that the original reset flag works; the
query/restore control proves the flag is lost at that API boundary. Earlier
expectations that forwarding could preserve the unqueryable flag failed on Apple
and are retained in CI history. Linux retains the original queried one-shot behavior.
