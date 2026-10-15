# Indexed Vulkan textures and graphics descriptors

The standard guest ICD now exports 108 public functions. This iteration adds
`vkCmdBindVertexBuffers`, `vkCmdBindIndexBuffer`, `vkCmdDrawIndexed`,
`vkCmdCopyBufferToImage`, `vkCreateSampler` and `vkDestroySampler`, and extends
existing descriptor layouts/updates/binding and push constants to graphics.
Original native Vulkan/MoltenVK performs the actual upload, fetch, sampling and
rasterization. GPL-3.0-or-later remains unchanged.

## Acceptance program

`tests/fixtures/vulkan-texture.c` runs unchanged against the original native Linux
loader and inside a Linux AArch64 guest through the unmodified Alpine loader.
It uploads a padded 2×2 texture, renders three 256×192 images and checks pixels
against independent nearest/bilinear equations (tolerance two channel values).
It covers:

- Float vertex attributes, nonzero vertex/instance binding offsets, per-instance
  input; the Linux `base-offsets` variant also checks nonzero `firstInstance`.
- UINT16 and UINT32 index buffers, nonzero `firstIndex`, zero base vertex and
  (in the Linux `base-offsets` variant) positive/negative `vertexOffset`, then a non-indexed draw.
- Combined sampler descriptor arrays, separate sampled-image and sampler
  descriptors, per-stage uniform buffers and shared vertex/fragment push constants.
- Padded staging rows, `VERTEX_INPUT` host-write/read barriers, explicit upload/layout transitions, nearest and linear
  filtering, descriptor replacement between completed submissions, readback and
  explicit noncoherent memory flush/invalidate.

## Local results

On the native Linux lavapipe oracle, 1-CPU and 4-CPU guests pass all three rounds;
exported PPM images match native output byte-for-byte. Khronos validation layer
1.4.309 with synchronization validation also passes without VUID/validation errors.
Existing compute and generated-vertex graphics regressions pass at both CPU
counts. The actual-driver lifecycle fixture covers 1560 output-unmap/backing-I/O
failure cases, including sampler publication rollback, without exhausting the
retained object table. Workspace strict Clippy and the iOS ARM64 cross-check pass.

Reproduce after building the standard fixtures and release runner:

```sh
bash scripts/build-vulkan-fixtures.sh
python3 tests/integration/vulkan-texture.py
cargo run --release --locked -p trames-vulkan --example lifecycle -- \
  tests/fixtures/bin/vulkan-compute.spv \
  tests/fixtures/bin/vulkan-render.vert.spv \
  tests/fixtures/bin/vulkan-render.frag.spv
```

Original MoltenVK 1.4.2 disables base vertex/instance draws in the iOS simulator;
its real end-command failure exposed this in the first Apple run (13 passed,
texture test failed). The standard acceptance program now defaults to base-zero
and tests full indexed/instanced texturing there. A separate `base-offsets` variant
keeps positive/negative vertex offsets and nonzero first instance covered on both
native Linux and 1-/4-CPU guests. No original driver code is patched or CPU vertex
emulation substituted. Triangle-strip primitive restart passes through natively.

Apple acceptance uses the same signed Alpine loader/guest program and original
MoltenVK over Metal in `LocalVulkanTests`. This iteration's Apple run is pending;
physical iPad acceptance has not been performed. Existing native swapchain and
floating-window acceptance remains a separate regression. No speed improvement
or complete Vulkan conformance is claimed.
