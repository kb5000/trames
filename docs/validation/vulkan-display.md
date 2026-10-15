# Native Vulkan display verification

The display path is original Alpine `libvulkan.so.1` → guest ICD → NativeCall →
original MoltenVK → native swapchain → the App’s `CAMetalLayer`. It performs no
per-frame GPU-to-guest readback, file export or UIImage reconstruction.
[Architecture](../architecture/vulkan-compute.md), [Issue #21](https://github.com/kb5000/trames/issues/21).

The [standard C fixture](../../tests/fixtures/vulkan-present.c) queries the display,
plane, mode, surface capabilities/formats/present modes and graphics queue support.
It renders 24 frames across two swapchains, checks old-chain acquire fails after
recreation, checks a second producer is rejected, destroys the device/surface,
then creates a fresh surface to check producer lease release. Per-image render
semaphores are reused only after reacquire; acquire semaphore reuse follows the
submission fence. A fence-only acquire path is also exercised. Prior-present
resource retirement waits for acquisition fence completion, including when
MoltenVK returns an index before availability. Continuous mode keeps drawing while the UI test moves, resizes,
hides/reopens the window, then closes the session during ongoing submissions.

The fixed Metal bounds/drawable extent and window clamping have explicit unit
coverage. Display capability tests verify null/invalid native inputs, generation
invalidation, exclusive producer state and retained leases after detach.

Linux workspace and offscreen regressions cover the unchanged compute/graphics
path without an attached Metal target. They cannot verify Apple presentation.
Actual Apple device/simulator linking and Metal/UI acceptance are pending for
this source revision; their results will be recorded after CI completes. A
simulator result is not physical iPad acceptance or an iPad performance claim.
