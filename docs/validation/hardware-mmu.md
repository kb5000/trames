# Hardware MMU and direct-mapped window cache

The ordinary RAM path now uses hardware-protected CPU aliases, native inline
operands and the same physical bytes as native functions. See the
[architecture](../architecture/hardware-mmu.md) for ownership, host-page grouping,
permission/fault rules, sparse addresses and preserved ARM concurrency semantics.
One non-JIT interpreter remains.

A per-view **16-entry, single-level direct-mapped window cache** avoids the sparse
map on hits. Cached absence also avoids repeated map searches after fork or in
unsupported windows. Creation invalidates the relevant absent entry. The existing
256-entry software permission TLB serves special mappings and precise faults.
Hardware admission failure goes directly to that fallback, without trying the
same MMU operand twice.

## Validation and code generation

Local checks: **375 workspace tests**, strict workspace/all-target/all-feature
Clippy, and **108 ARM64/QEMU CPU/memory tests** pass. Memory tests include actual
host access faults for every operand width, simulated 16 KiB mixed-permission
subpages, COW/remap/discard, cached collisions/absence and stale owner generations,
retained worker snapshots and native/CPU coherence. Isolated subprocesses check
unrelated default host faults and previous handlers' masks/NODEFER/one-shot rules.
The default child explicitly restores SIG_DFL so a sanitizer's crash policy does
not change that assertion. Linux-user children re-exec through QEMU when binfmt
is unavailable. Signal forwarding now explicitly reconstructs the previous handler mask from the
interrupted context, including when an interposed dispatcher supplies another
mask. Default redelivery first unblocks its signal. The host memory suite passes
these paths; the complete instrumented-standard-library TSan batch passes 88
tests before the final default-redelivery adjustment. TSan does not instrument
raw asm bytes; its result checks the Rust-side region/ownership protocol, not
every native memory instruction.

Real guest mmap/EOF/SIGBUS/COW/code aliases, edited musl signal contexts,
same-page 1/2/4-CPU regions, 507 independent warmed integer snapshots and all 19
native pthread entries/nested callbacks pass. These broader workflows were run on
the first inline candidate; the refined fallback additionally passes the full
workspace and ARM CPU/memory suite. Exact-commit CI covers the final source.

The final iOS ARM64 Release core compiles to assembly on Linux, including its real
worker interpreter instantiation. [Selected handlers](hardware-mmu/arm64-selected.s)
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
shifts for hot group indexing. This change and signal-forwarding fixes are being
validated; their performance is not inferred from the earlier binary's numbers.
All earlier unfavorable measurements remain. Physical iPad MMU speed is unmeasured.

Initial exact-source CI caught signal-composition issues: Linux/TSan deferred
signal masks, and Darwin one-shot-handler default redelivery. These failures are
recorded rather than claimed as passing pipelines. A new early native Apple
memory-ABI job precedes the app build, while full Linux/Apple validation is rerun.

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
