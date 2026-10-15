# Vulkan compute verification

Tracker: [Issue #21](https://github.com/kb5000/trames/issues/21).

The standard path uses unmodified signed Alpine Vulkan loader 1.4.313.0-r0,
the Trames guest ICD and the actual native Vulkan driver. With 1 and 4 virtual
CPUs, three successive SPIR-V submissions each transform 4096 uint32 values as
`value = value * 3 + 7`. Every value matches the identical native C fixture.
The program exercises device queries/function pointers, allocation/binding,
flush/invalidate, push constants, descriptors, commands/fences, reset, zero/finite
timeouts, queue/device idle and resource teardown. Two additional chains each
perform three dependent specialized dispatches through reusable binary
semaphores, including two records in one queue submission. Every uint32 matches
the identical native fixture. Two specialization IDs use nonzero, out-of-order
data offsets. Semaphore-only submissions, partial mapping offset translation,
remap persistence and unaligned map-pointer alignment also pass. Guest-only
invalid-ID/range/stage tests verify rejection before native submission and
preservation of semaphore state after rejection. Reopening with the ICD still
installed but host `--vulkan` absent returns incompatible driver.

The actual embedding ABI also passes automatic ICD installation, original signed
loader installation, all three compute rounds and teardown. Existing sudo,
anonymous fd execution/Python, signals/resource limits and embedding persistence
regressions pass with the final runtime.

The local reference is Mesa Lavapipe/llvmpipe using LLVM 19.1.7, a software
Vulkan driver. This verifies protocol and compute behavior, not iPad GPU speed.
The workspace passes 283 Rust tests covering malformed transport, object domains,
shared allocation accounting, code mutation and independent CPU gating as well
as existing functionality. Formatting and strict Clippy pass. iOS FFI compiles
with the actual static MoltenVK device archive selected; that compile check is
not an Apple SDK link or hardware run.

Apple CI builds device/simulator apps and runs `LocalVulkanTests` through the
real embedding ABI. It installs the original signed loader from bundled packages,
interprets the same ARM64 fixture, requires correct third-round results and
rejects the llvmpipe software reference. Exact-source CI and physical iPad
acceptance remain pending on the issue. The initial-source Apple run
[38614884248](https://github.com/kb5000/trames/actions/runs/38614884248)
stopped before Apple compilation because the Linux prerequisite lacked `rg`;
the ICD dependency check now uses portable awk. Static Rust library dependency
flags are also declared explicitly in Xcode for the final MoltenVK/C++/framework
link. The initial-source Linux run
[38614884471](https://github.com/kb5000/trames/actions/runs/38614884471)
passed the initial Vulkan integration but exposed an older FIFO open/PTY
interaction in M6 regression. Pending FIFO descriptors now complete their
rendezvous before terminal inspection. A signal interruption regression and
two independent original M6/QEMU comparisons pass locally.

## Reproduction

Install the AArch64 toolchain, `libvulkan-dev`, `mesa-vulkan-drivers` and
`glslang-tools` on Linux, then run:

```sh
bash scripts/build-vulkan-fixtures.sh
cargo build --release -p trames-cli -p trames-ffi --locked
export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
python3 tests/integration/vulkan-compute.py
```

Select the actual `lvp` manifest filename on your distribution. The guest gets
its own manifest through explicit `--env`; the two paths are distinct.
`GLSLANG_VALIDATOR`, `VULKAN_HOST_LINK`, `MUSL_CC` and `TRAMES_RUNNER` permit
explicit tool paths. The ICD must use dynamic guest libc: embedding static musl
creates private loader/auxv state and prevents proper driver discovery.

`prepare-vulkan.py --apple` verifies native binaries and source hashes.
`build-ios.sh` selects the actual target's static MoltenVK archive, packages the
verified guest ICD and retains notices. iPad users enable the experimental
switch for a new session and install unchanged `vulkan-loader` with apk.

This profile excludes graphics/images/presentation, coherent zero-copy mapping,
custom allocation callbacks, application extensions, timeline semaphores and
multiple queues. Synchronous creation remains a coordinator cost.
[Architecture](../architecture/vulkan-compute.md) and
[binary/input/log hashes](vulkan-compute-data.json) define the current scope.
