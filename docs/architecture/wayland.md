# Native Wayland compositor

Original ARM64 Linux clients use their ordinary libwayland and connect to a host
Rust compositor built from wayland-rs and upstream protocol definitions. There
is no nested guest Weston compositor. Each xdg_toplevel becomes an independently
managed iPad window: move, resize, minimize, close or maximize into a workspace
tab. Resizing sends configure/ack and requires new client buffer dimensions;
it does not retain and scale a fixed-resolution canvas.

The implementation is in `feature/native-wayland`, [PR #26](https://github.com/kb5000/trames/pull/26),
tracked by [Issue #25](https://github.com/kb5000/trames/issues/25). It has not yet
been released. See the [usage guide](../guides/wayland.md).

## Shared storage and descriptors

Managed guest memfds have a real anonymous host inode. The Unix stream bridge
carries bounded control messages and SCM_RIGHTS capabilities, never pixels or
host paths. Connection identity comes from the guest bridge. Virtual descriptor
identity, credentials, logical EOF/seals, mmap dirty/COW hooks and existing guest
memory synchronization are preserved. The shared physical-storage budget is
128 MiB per instance.

An export lease is registered before sending SCM_RIGHTS and survives until
SharedFile import acknowledges the descriptor or its client disconnects. Closing
the last guest descriptor while an FD is in transit therefore cannot lose its
logical EOF, managed backing or budget. Cancelled transfers cannot consume the
acknowledgement for a later export of the same inode. Backpressure is bounded
independently of frame leases.

Mappings retain storage independently of descriptors. Logical shrinking zeros
removed bytes without physically truncating pages still visible to native readers
or GPU work. Physical storage remains charged until the final mapping/frame lease
is released. A wl_buffer shared across surfaces has one common usage lease.

The Metal frontend imports page-aligned SHM bytes with bytesNoCopy and handles
arbitrary row strides in its fragment shader. A refused import uses an explicit,
counted copy fallback. Frames retain their bytes through Metal command completion
and remain valid across VM shutdown. Mode-zero fallocate reserves guest storage
quota and grows files while preserving data and file position. KEEP_SIZE,
hole-punching and physical host-block reservation are not implemented.

## Vulkan presentation

Standard VK_KHR_wayland_surface uses the original guest loader/libwayland. The
ICD resolves libwayland through its own handle, including clients using RTLD_LOCAL.
It authenticates the bridged connection and resolves its connection-local
wl_surface ID; guest pointers never become host capabilities.

Swapchains own bounded pools of original native Vulkan optimal images. On Apple,
VK_EXT_metal_objects exports their MTLTextures for direct Metal sampling. Producer
fences gate publication without blocking the VM coordinator. A frame lease spans
producer completion, queued/current scenes and final Metal command completion.
Image reuse follows the final lease, including swapchain replacement and session
teardown. FIFO pauses while minimized/occluded. Pending frames are bounded across
pool replacement; allocations share the existing 128 MiB GPU budget.

Normal presentation performs no pixel readback or GPU-to-CPU-to-GPU transport.
Only debug acceptance reads back the final host drawable for pixel verification.
CPU mappings remain separate guest/native storage: native HOST_COHERENT types
synchronize changed bytes at submit, successful fence status/wait and unmap,
preserving independent CPU/GPU changes. Those shadow transfers are distinct from
image presentation. See [Vulkan memory and lifetime](vulkan-compute.md#memory-and-lifetime)
for the profile's boundaries and accounting.

The graphics subset includes sRGB textures, D16 depth attachments, depth
compare/write, empty native pipeline caches, PREINITIALIZED images and
SIMULTANEOUS_USE command buffers with per-submission completion accounting.
Native Wayland and legacy fixed-display WSI remain separate instance routes;
requesting both on one instance is rejected.

## Scene and input

Synchronized subsurfaces publish buffers/positions atomically with their parent.
Input regions retain copy semantics. Buffer scale/transform, window-geometry
origins and nonzero v4 attach offsets are preserved. Popup placement supports
anchor/gravity and ordered flip/slide/resize constraints, explicit reposition
tokens, authenticated implicit grabs and outside-click dismissal. Surface role
identity survives role-object destruction.

The native canvas includes popup extents beyond root geometry, clipped to the
actual workspace before drawable allocation. Empty padding is transparent and
does not intercept neighboring controls. Pointer/caret coordinates include that
scene origin. Pointer grabs deliver release outside the originally pressed region;
native focus loss clears held keys/buttons and stale text contexts.

A native seat sends Linux evdev keycodes. The original xkeyboard-config US keymap
is protocol data; native composition uses UIKit UITextInput and text-input v1/v3.
The local document uses UTF-16 and converts to protocol UTF-8 byte offsets for
preedit, commit, deletion and cursor ranges. Context IDs/epochs reject late edits
after focus changes. Surrounding text and caret rectangles return from the client.
Weston editor uses v1; an independent original C client exercises v3.

v3 surrounding deletion excludes selected text; preedit/commit replaces the
selection itself. UIKit avoids duplicate deletion and retains selection direction.
v1 preserves explicit cursor-relative byte deletion. Marked-text backspace acts
at the composition caret, including interior selections.

## Validation

- 182 affected shared/core/VFS/Vulkan/FFI/Wayland library tests and strict Clippy
  pass on Linux.
- Four independent original C acceptance groups cover two-window resize/close,
  synchronized scenes/popups/input regions/grabs, v3 UTF-8/serial/surrounding state,
  producer readiness/FIFO/budget and retained GPU-image lifetime.
- Original Alpine-signed wayland-info, Weston SHM/editor and vkcube run through
  the production session/transfer/frame ABI. vkcube recreates images at 320x240.
  Retained SHM/GPU capabilities survive session shutdown and then release cleanly.
- Compute (including coherent unmap/remap and persistent mappings without explicit
  flush/invalidate), graphics and texture outputs match the independent native
  Linux driver with one and four guest CPUs; graphics pixels match byte-for-byte.
- Both Apple apps link. Actual Apple simulator execution passes original
  wayland-info, two-window resize, no-copy SHM drawing and UIKit Chinese/emoji
  composition. Original vkcube reaches actual Metal drawables, recreates its
  swapchain at 320x240 and produces nonuniform displayed pixels. The coherent
  no-flush/no-invalidate compute test also passes. Apple acceptance is 23 passed,
  zero failed/skipped; see the [validation record](../validation/wayland-native.md).
  This is simulator execution, not an iPad device test.

Reproduce independent native C acceptance with the original development tools:

```sh
bash scripts/test-wayland.sh
```

On Apple, that script builds original Wayland 1.23.1 client/scanner sources with
a documented Darwin OS adapter: fcntl CLOEXEC, SO_NOSIGPIPE, getpeereid, original
recvmsg CLOEXEC fallback and omission of unused server epoll helpers. Protocol,
queue and marshalling sources stay unchanged; this test library is not linked
into the app. Linux uses the original distro library.

To prepare signed offline inputs and exercise the production ABI on Linux:

```sh
bash scripts/build-vulkan-fixtures.sh
python3 scripts/prepare-wayland-guest.py
cargo build -p trames-ffi --release --locked
TRAMES_WAYLAND_STORE=/tmp/trames-wayland-test python3 tests/integration/wayland.py --prepare
```

The store must be absent for --prepare. Omit that flag to repeat client/lifetime
checks against a prepared store. Ordinary guest apk resolves and verifies the
original repository, choosing between alternative providers itself. Weston CSD
PNG assets come from Alpine's `weston` package; `weston-clients` alone lacks them.
The headless ABI test does not validate Apple pixels or system keyboard behavior.
Apple CI uses these same original inputs against real CAMetalLayer drawables,
UIKit input and native configure-driven image recreation.

## Initial limits and upstream notices

This is a selected protocol/graphics implementation, not full Wayland or Vulkan
conformance. Output scale is initially one. Clipboard/data-device/DnD, custom
cursor images, automatic reactive popup relayout, touch protocol and EGL/OpenGL
bridging are not implemented. Device touch and actual system keyboard experience
still require iPad use.

wayland-backend is vendored under its original MIT license with three documented
Apple cfg changes. See its TRAMES-PATCHES.md and the [dependency record](../security/dependencies.md#native-wayland-and-shared-descriptors).
Original dependency, XML and keymap notices are bundled in the App. The offline
Wayland repository/client fixtures serve DEBUG XCTest acceptance and are omitted
from sideload IPA packaging; runtime clients are installed with ordinary apk.
