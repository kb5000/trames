# iPad 1.3.0 build 35: memory and Xwayland crash fixes

2026-10-22. Implementation is on main; the published 1.3.0 build 35 IPA is
unchanged. No raw iPad report, personal identifiers, addresses or Firefox
logs/screenshots/stacks/dumps are published.

## Firefox host allocation abort

The device report records a Rust host allocation abort in
`parallel::prepare_frame`, rather than a recorded jetsam kill. A reproducible
memory amplification is fixed in [783271b](https://github.com/kb5000/trames/commit/783271b582a36791aa65d80c6597c82f5fdd124d):
after parallel publication/fork, untouched anonymous pages may have mapped/shared
byte owners without resident charges. Hardware MMU host-group relocation copied
those logical zeros into new physical RAM. Normalize still-uncharged anonymous
owners to demand-zero before relocation. Backed/resident pages, first-write
charges, fork isolation and existing permissions retain their rules. Ordinary
CPU/native loads/stores gain no checks, locks or barriers.

The new regression fails before the fix. [Linux/Apple ARM64 CI](https://github.com/kb5000/trames/actions/runs/39656197539)
passes Memory 94 on each platform, relevant strict Clippy and iOS FFI. Coverage
includes 16 KiB host groups, actual Linux `mincore` nonresidency, first real
write charging, adjacent pages and both directions of fork isolation.

A supplemental production Release Firefox launch uses four CPUs, 4 GiB
aggregate/per-process quotas, default preferences/sandbox and Wayland proxy,
with explicit `LIBGL_ALWAYS_SOFTWARE=true` only for the Linux Lavapipe reference.
A window appears at 66.55 seconds; at 180 seconds the target JS/native-GPU page
gate still fails (SHM only). No host allocation abort occurs in that interval;
final guest diagnostics are incomplete. This is **not** a Firefox GPU pass or
proof that all physical iPad memory pressure is resolved. Raw diagnostics remain
private. Physical iPad startup and the known default-refresh issue remain open
in [#33](https://github.com/kb5000/trames/issues/33).

## Fragmented host address space and page metadata

2026-10-24: a separate physical iPad launch with 4 GiB guest quotas reached a
host allocation abort while growing the coordinator's page descriptor array.
The requested contiguous allocation was about 27.5 MiB. At the stopped process,
a 1 MiB allocation succeeded, while both malloc and a direct system mapping of
the larger size failed with ENOMEM. Physical footprint was about 1.34 GiB and
the system still reported about 3.66 GiB available to the process. The largest
unmapped interval between existing mappings was about 4.64 MiB. Virtual mapping
totals include large inaccessible system reservations and are not RAM usage.
This evidence identifies a contiguous host allocation bottleneck; it does not
establish that guest residency quotas or physical RAM were exhausted.

Coordinator descriptors and recycled indices now use 256-slot blocks. Worker
frames, root-to-worker positions and full-snapshot mapping lists use the same
storage, avoiding another guest-sized contiguous allocation at publication.
Guest page indices, translation invalidation, permissions, COW, resident
accounting and worker synchronization retain their existing contracts. Only
the compact directory of blocks grows contiguously. Genuine host exhaustion
can still fail an allocation; this change is not a general OOM recovery policy.

The allocation regression maps a 64 MiB VM, checks a native copy across a block
boundary, publishes a worker view, forks with COW, and unmaps/reuses slots before
republishing. It measures real host allocator requests for mapping, fork and
publication and requires each to stay at or below 128 KiB. The original code
fails this test with a 1,179,648-byte request; the block implementation passes.
Downstream Linux guest unit tests have 120 passes and one ignored test with
local networking enabled. One additional test,
`retained_host_interest_rearms_without_reopening_and_actors_remain_independent`,
fails at `UnixStream::set_read_timeout` with EINVAL on this macOS host; the same
single test fails with the original memory implementation. This remains a
platform-test limitation, not a passing check.

The first physical retry passed the large-array growth but later failed while
forking, even on an 18 KiB block allocation. About 5.58 GiB of optional physical
storage and CPU aliases was still retained. Untouched anonymous zeros were being
promoted into mapped storage during worker publication, pinning whole physical
VA windows over fork despite having no resident charges. Publication now keeps
these pages on the existing demand-zero shared storage path. First real writes
retain the same charge and region protocol. Coordinator TLB fills verify the
physical owner/offset before exposing a CPU alias, so software materialization
cannot leave a stale zero-filled alias visible.

A new fork regression proves that retired physical owners disappear when all
pages are untouched, then checks first-write charging and parent/child isolation.
It fails with the original code. A second regression warms a coordinator TLB,
performs a software zero page's first worker write, commits it and checks byte
coherence in both directions. The physical-alias fixture explicitly materializes
its host groups before publication; it still requires actual hardware aliases
and checks native/CPU visibility, dirty commit and permissions.

Final native Apple ARM64 memory validation has 106 unit tests, four translation
integration tests and the allocation regression passing (111 total), plus strict
Clippy. CPU 43 and Core 171 also pass (two manual Core benchmarks ignored).
Strict iOS memory Clippy and the signed iOS FFI build pass. During the physical
retry, a checkpoint at roughly three minutes measured about 1.04 GiB of MMU
physical/CPU virtual reservations and 1.35 GiB physical footprint, without the
previous host abort. The final MMU reservation sample was about 1.21 GiB. These
are samples, not peaks or a matched performance comparison.

The final binary was observed with plain `firefox` for at least seven minutes,
then with explicit `MOZ_ENABLE_WAYLAND=1 GDK_BACKEND=wayland`, a newly created
profile and `about:blank` for at least eight minutes. Both retained GPU support,
default preferences and sandbox/proxy settings. Neither reproduced the host OOM,
but neither displayed a browser window. The first run was interrupted with
Ctrl-C; the second was intentionally ended after diagnostic capture. Neither is
normal-shutdown acceptance, and no full startup, page or GPU acceptance is
claimed. Trames Dev was reopened to an idle terminal afterward.

[Linux CI](https://github.com/kb5000/trames/actions/runs/40008374911) initially
passed native workspace tests, strict Clippy, instrumented TSan and allocator
ASan/UBSan checks, then failed five Core worker/statistics assertions under the
independent ARM reference. The unmodified `dac3506`
[baseline run](https://github.com/kb5000/trames/actions/runs/39990963812) fails the
same five tests. A retry failed the cohort wait/wake/yield assertion in native
workspace tests; the unmodified baseline also reproduces that assertion locally
in 8 of 100 repeated native ARM runs under concurrent load. These checks remain
failures, not waived passes. [Apple memory/ABI CI](https://github.com/kb5000/trames/actions/runs/40008374910)
passes. The Apple app job later stopped before simulator linking because its
downloaded Vulkan payload lacked the expected ICD, manifest and license files.
Its native tests passed; simulator/device packaging did not complete. The PR
stays draft while complete Firefox acceptance and CI follow-up remain open.

Device identifiers and original logs, screenshots, stacks and mapping dumps
remain local.

## Physical startup progress and repeated TCP snapshots

The next physical investigation distinguishes slow execution from a global
deadlock. Guest process/thread lists and retired instruction counters continue
to grow, including socket and content processes, while temporal samples show
workers executing the interpreter. Sleeping futex and I/O tasks are also present;
their existence alone does not identify a deadlock. The browser still has not
submitted a visible window in this diagnostic run.

Across 32 debugger-assisted samples, coordinator preparation repeatedly appears
alongside real worker execution. Four samples stop in host `getpeername` while
constructing TCP capabilities. Eligibility checks for unrelated poll/Unix waits
scan native sockets, and dispatch rebuilds the same native TCP snapshot for
threads sharing an unchanged FD table. This is concrete redundant preparation
work, not proof that all remaining Firefox delay has the same cause. Debugger
pauses and different profile/cache states prevent a startup speedup claim.

Worker eligibility now checks ordinary retained resources first, inspecting only
the requested FD when native TCP byte I/O needs a capability. Dispatch shares
one TCP snapshot per descriptor identity during its stopped preparation phase.
The cache retains that identity to prevent pointer reuse and is discarded before
workers start. Every new cohort rechecks connection, timeout and firewall state,
including changes that do not alter FD membership. Existing weak socket
capabilities, accepted-operation leases and synchronization remain unchanged.
No lock, barrier, guest preference, refresh rate or sandbox adjustment is added.

Local Apple ARM64 validation passes 171 Core tests (two manual benchmarks
ignored), both TCP capability tests, 20 Linux direct-I/O tests, and strict Clippy
for the affected Core/VFS crates on macOS and iOS. The TCP regression checks shared capture reuse, fresh
connection discovery without FD mutation, invalid descriptors and last-close
behavior. Dependency-wide macOS linting additionally encounters unchanged
`needless_return` warnings in `trames-native`; the scoped checks use `--no-deps`.
The signed iOS app builds and installs successfully.

The physical retest uses a new profile, explicit Wayland and `about:blank`, with
the existing 4 GiB quotas and default browser preferences. An initial two-minute
uninterrupted interval retires about 3.51 billion guest instructions and reaches
content-process startup, without a visible window or host allocation abort.
Observation continues for roughly five minutes in total, with brief diagnostic
pauses between intervals; the counter reaches about 4.54 billion instructions,
but the final Wayland window list is still empty. The app is then deliberately
restarted to an idle terminal. Normal browser shutdown, complete startup, page
and GPU acceptance remain unverified.

A later 32-sample burst finds 11 coordinator samples in memory moves, chiefly
creating/moving cohort jobs and views. Of 256 worker observations, 250 wait for
the next pool command; others execute guest memory/interpreter operations. No
coordinator leaf sample in this burst is `getpeername`, but differing phases and
the small sample size prevent a numerical before/after CPU or startup claim.
Remaining investigation should quantify repeated cohort construction and large
state moves, while retaining the original memory/concurrency model. Elapsed
runtime timers can span debugger pauses and overlap each other; they are not
host CPU occupancy or an additive decomposition of startup time.

Source commit `8a03fc0` [Linux runtime](https://github.com/kb5000/trames/actions/runs/40059795178)
passes workspace tests, strict workspace Clippy, instrumented TSan, native
allocator sanitizers and iOS compilation, then fails the same five independent
ARM worker assertions recorded above on the unchanged baseline. Its
[Apple run](https://github.com/kb5000/trames/actions/runs/40059795207) passes
memory/ABI and guest inputs; app packaging stops on the same missing downloaded
Vulkan payload files. These are failures, not complete CI passes.

## Compact execution-view handoff

The next change keeps each approximately 6.4 KiB worker View in an independently
owned Box throughout preparation, queueing, parking and service handoff. Growing
the preparation vector and moving Work/Job values now move its pointer. The
vector reserves its known member count. All VM Views are still prepared and
admitted before execution, with the same snapshot identity/user checks and
private-page modes. Joining still drops every View before coordinator mapping
operations. Interpreter/native execution receives `&mut View` as before: its
translation layout, hot loads/stores, region guards and permissions do not change.
This trades an additional small allocation per prepared View for fewer large
copies; it does not retain a live View over a mapping checkpoint.

The ignored `process::tests::cohort_preparation_profile` benchmark executes the
real coordinator and eight-worker handoff with one runnable task and 127 real
futex waiters. Each of 4,096 measured rounds performs one ordinary owner syscall;
both binaries must retire exactly 20,480 guest instructions, preserve all tasks,
leave every waiter asleep and keep the shared sentinel unchanged. Setup and
16 warmup rounds precede timing. This measures repeated cohort construction,
dispatch and return, including the new allocations; no guest/browser ELF is
substituted.

Five alternating Release pairs on the native Apple ARM64 host give a median of
0.901573 s before and 0.569813 s after (about 36.8% less elapsed time). Before
samples span 0.889390–1.094214 s; after samples span 0.535084–0.579228 s. These
small, isolated samples establish a benefit for this fixed scheduling workload,
not a Firefox startup ratio or a physical iPad timing guarantee.

The existing 171 Core tests pass, with three manual benchmarks ignored. Strict
scoped macOS/iOS Core Clippy passes, as does the signed iOS build/install. The
service regression continues to verify that the exact View is retained across
worker parking and service completion. CPU, Memory, guest graphics and browser
preferences are unchanged; their earlier checks are reused.

The signed physical candidate runs with the same 4 GiB quotas, eight configured
CPUs, a fresh profile, explicit Wayland and `about:blank`. After approximately
two and a half uninterrupted minutes, graphics initialization has completed
without a host allocation abort, but the Wayland window list remains empty.
That is not complete browser startup or GPU acceptance. A subsequent attempt
to collect coordinator syscall breakpoints stalls the host debugger in its
register/unwind machinery and yields no usable call histogram. It is excluded
from guest timing and liveness evidence; the debugger is stopped and Trames Dev
is restarted to a responsive idle shell.

A separate safe heap-initialization experiment has only a few percent median
difference in five further alternating pairs with overlapping sample ranges;
it is not retained. The delivered change is the measured View ownership and
handoff change above, not a claim that every remaining Firefox wait is fixed.

## Original mousepad X11 save dialog

Three concrete native graphics gaps were corrected, using the original
Mesa/Zink and pinned MoltenVK/SPIRV-Cross implementations:

1. Legacy Mesa uses `ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT` in render passes even
   without exposing that extension; its barrier/descriptor path already selects
   core GENERAL. [dc88369](https://github.com/kb5000/trames/commit/dc883698193b41872672a685333291dac496940b)
   retains legal per-reference GENERAL/depth-read-only layouts and aligns the
   legacy enum with GENERAL. Old guest payloads retain their default layouts;
   the EXT is not advertised. Descriptor decoding is unchanged.
2. The cloud Apple Paravirtual device rejects transparent-black border-color
   samplers. [9991a00](https://github.com/kb5000/trames/commit/9991a0064076aa5f0239826d22c78ad7cdd662a7)
   uses original Metal ClampToZero for mutable core transparent-black borders.
   Other colors and upstream constexpr handling are unchanged. Native Metal
   nearest/bilinear/outside-texel/RGB/alpha pixel checks pass; this correction
   alone does not fix the modal crash.
3. The failing shader writes a vertex vec4 at location 0 but consumes two
   fragment vec2 variables at components 0 and 2. Vulkan links components;
   old MSL creates a `locn0_2` fragment input with no vertex counterpart.
   [b2ff6b5](https://github.com/kb5000/trames/commit/b2ff6b5a0b6223908490c69e2cb929ab597a2d68)
   reuses SPIRV-Cross's existing location packing/swizzle code for compatible
   32-bit vertex outputs/fragment inputs. Arrays, matrices, mixed scalar types
   or interpolation qualifiers retain previous handling. Transform feedback,
   including native Xfb mode, is excluded from this new path.

The pinned original compiler reproduces the unmatched interfaces in both
packed-to-split and split-to-packed directions. The patched compiler produces
one matching vector and the required xy/zw swizzles. There is no replacement
shader algorithm, CPU rasterizer, extra production pixel copy, per-draw barrier
or falsely advertised feature. GPU remains the default. Disabling texture-barrier
extensions, forced synchronization, classic Metal descriptor binding, disabling
optimized pipelines and GTK server decorations were unsuccessful diagnostics;
none becomes a product setting.

## Final targeted verification

[Final Linux/Apple Metal run](https://github.com/kb5000/trames/actions/runs/39675520457)
is successful for production sources b2ff6b5:

- Both new component shader pairs pass the existing independent three-round
  barycentric/color/alpha/scissor GPU readback oracle on Linux and Apple:
  294,912 readback pixels per platform; ambiguous triangle edges are excluded
  from the numerical oracle, which checks over 90% of each image.
- The old native runtime is required to fail first. It reaches
  `vkWaitForFences` / device lost and guest abort, rather than failing to find
  an input file. The new runtime passes the same inputs.
- Original signed mousepad, original glamor and Mesa complete two
  edit/unsaved-close/modal-present/cancel cycles with guest defaults (host tracing
  and Metal API validation enabled for this test). The
  exported actual Metal textures pass a nonblank-content check. That check
  is not an exact UI screenshot oracle. The complete component-plus-dialog
  probe takes 56.60 seconds; this is functional acceptance, not a benchmark.
- New/old native pipeline-cache UUIDs differ with the original build revision.
- The synthetic Metal evidence ZIP SHA256 is
  `210786c0f2cb01f59ffc4cd683617ded314e4ec54441bf708c978e38d1465da5`.
  ZIP CRC, result JSON, negative-reference failure and native patch hashes
  were checked after download.

Vulkan 31 and relevant strict Clippy were already passed; unchanged Rust
checks, Memory94, TF, other graphics, CPU, Linux, network and frontend suites
are reused. The earlier four GENERAL/legacy-feedback render-pass pixel modes
also passed native Metal. The complete source verifier now applies all native
patches to the SHA-verified pinned archives; the source bundle already includes
the whole patch directory, original dependency archives and build recipe.
Later records/source-verifier-only changes do not alter the tested native
shader or memory implementations and do not repeat runtime tests.

The Apple runner uses a paravirtual Metal GPU, not the owner's physical M3.
Real iPad mousepad/Firefox retesting is still needed. Optional missing Zink
features are not claimed implemented. [#35](https://github.com/kb5000/trames/issues/35)
remains open for broader X11/GLX/input and physical-device coverage.
