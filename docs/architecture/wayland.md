# Native Wayland compositor (work in progress)

The guest runs original ARM64 Linux libwayland clients. A host Rust event loop
uses the original wayland-rs server/backend and upstream protocol definitions;
it does not emulate a Linux display driver or run a nested guest compositor.
Each xdg_toplevel is a native app window, with movement, minimization, close,
and maximization into a workspace tab. Resizing sends configure/ack and requires
new client buffer dimensions. It does not scale an unchanged fixed canvas.

## Storage and lifetime

Managed guest memfds have a real anonymous host inode. The Unix stream bridge
carries control messages and SCM_RIGHTS capabilities, not pixels or host paths.
Virtual descriptor identity, credentials, EOF/seals, mmap dirty/COW hooks and
existing guest memory synchronization are preserved. Mappings retain storage
independently of open descriptors. Logical truncation zeros removed bytes but
never physically removes pages while native readers or GPU work can retain them.
Physical bytes remain charged against the instance's memfd budget until the
last native mapping/frame lease is released.

The Metal frontend imports page-aligned SHM bytes with bytesNoCopy and reads
arbitrary row strides in its fragment shader. If the device refuses an import,
a bounded copy is explicit and counted in the canvas accessibility statistics.
No-copy support and rendering still need actual Apple execution validation;
compilation on the iOS Rust target alone is not that validation. A frame owns
its buffer leases through command-buffer completion, including session teardown.
A wl_buffer shared by several surfaces has one common usage lease; its release
cannot race another surface's continued use.

Vulkan uses VK_KHR_wayland_surface and the original guest loader/libwayland.
The ICD resolves libwayland through its own library handle, including clients
that load it RTLD_LOCAL. The bridge authenticates the socket and resolves its
connection-scoped wl_surface ID; guest pointers are never native capabilities.

Swapchains own a bounded pool of original native Vulkan optimal images. On Apple,
VK_EXT_metal_objects exports their original MTLTextures, sampled directly by the
Metal window compositor. A nonblocking producer fence gates publication. A frame
lease spans producer completion, queued/current scenes and final Metal command
completion; image reuse follows the final lease, including swapchain replacement
and session teardown. FIFO presentation pauses while minimized/occluded. Pending
frames are bounded per surface across pool replacement; image allocations share
the existing GPU budget. The VM coordinator never waits for a native frame/fence.
No normal pixel readback or GPU-to-CPU-to-GPU transport is used. Debug acceptance
alone reads back the final host drawable to verify actual displayed pixels.

The expanded selected graphics path includes sRGB textures, D16 depth attachments,
depth compare/write, empty native pipeline caches, PREINITIALIZED images and
SIMULTANEOUS_USE command buffers with per-submission completion accounting.
Native Wayland and legacy fixed-display WSI are currently separate instance
routes; enabling both extension routes on one instance is explicitly rejected.

## Input

A host seat sends Linux evdev keycodes and surface-local pointer coordinates.
The original xkeyboard-config US keymap is a protocol data artifact; native
composition uses UIKit UITextInput and upstream text-input v1/v3 instead.
The native document uses UTF-16 positions internally and converts to protocol
UTF-8 byte offsets for preedit/commit/delete. Context IDs and epochs reject
late IME edits after focus changes. Surrounding text and caret rectangles travel
back from the client. Weston editor's text-input-v1 is included explicitly.

## Current validation and remaining work

- Host shared-storage/mapping/accounting tests and VFS regressions pass locally.
- An independent C client linked to original libwayland-client creates two xdg
  windows, closes its buffer FDs/mappings immediately, changes actual buffer
  resolution after configure, receives frames, and exits on native close.
  Run `bash scripts/test-wayland.sh` with the original development tools installed.
- Original Alpine-signed wayland-info and weston-simple-shm run in the guest;
  simple-shm continuously redraws and exits on the native close event. Weston
  editor completes native text-input-v1 preedit/Chinese/emoji commit roundtrips
  after installing its original Weston PNG assets. Reproduce with the ignored
  `trames-core/tests/wayland.rs` acceptance and a prepared signed-package store.
- The same ARM64 client and original Alpine libwayland-client/libffi execute
  through the Linux guest VFS bridge successfully.
- Rust core/bridge/FFI compile for aarch64-apple-ios. Swift parser checks pass;
  the Apple simulator app now links successfully; actual Metal/IME runtime acceptance remains pending.
- Synchronized subsurfaces, input regions, buffer transforms, nonzero v4 attach
  offsets, real window-geometry origins, and native IME serial/ready state are
  implemented. Original Alpine vkcube 1.4.313 completes standard Wayland WSI
  presentation and native configure-driven 320x240 swapchain recreation on Linux
  lavapipe. This is protocol/resource validation, not Apple Metal validation.
- Independent C acceptance also proves producer readiness, FIFO acknowledgement,
  queue budget recovery and retained image lifetime across native-server teardown.
- scripts/prepare-wayland-guest.py prepares original signed runtime packages, the
  C ABI peer and original Weston PNG assets. Apple CI installs them with the
  ordinary guest apk, then exercises two windows, Weston SHM/editor and vkcube
  against actual CAMetalLayer drawables, including native UITextInput composition
  and configure-driven image recreation. Pixel readback is debug-test-only.
- Actual Apple UI test results are still being completed. This is not a completed release.

Upstream wayland-backend is retained under vendor/ with its MIT license and
three documented Apple-platform cfg changes. See TRAMES-PATCHES.md there.

Mode-zero fallocate now reserves guest storage quota and grows a file while
preserving existing bytes and file position, so original libwayland/Weston SHM
allocation works. KEEP_SIZE/hole-punching and physical host-block reservation
are not claimed. The original Weston CSD PNG assets come from the `weston`
package; `weston-clients` alone does not include them in Alpine v3.22.

The Apple C acceptance peer builds original Wayland 1.23.1 client/scanner sources
with a documented Darwin OS adapter: fcntl CLOEXEC, SO_NOSIGPIPE, getpeereid,
original recvmsg CLOEXEC fallback, and omission of unused server epoll helpers.
Protocol/queue/marshalling sources are unchanged; this test library is not
linked into the app. Linux acceptance continues to use original distro binaries.

An additional original C scene peer validates synchronized child buffer/position
atomicity, destroyed-region copy semantics, CSD geometry offsets, input-region
hit testing, pointer release outside the pressed surface, popup anchor/gravity,
ordered flip/slide/resize placement, explicit reposition tokens and authenticated
keyboard/pointer implicit grabs with outside-click dismissal. Surface role
identity survives role-object destruction. The native canvas includes popup
extents beyond parent geometry, clipped to the actual app workspace before GPU
allocation, and translates pointer/caret coordinates with that scene origin.
Automatic reactive popup relayout, custom cursor images, data-device clipboard
and drag-and-drop, touch protocol and full Wayland conformance are not claimed.

Current Linux regressions: 180 shared/core/VFS/Vulkan/FFI/Wayland library tests, four
independent native C acceptance tests, and original graphics/texture guest pixel
oracles for one and four CPUs pass. Ordinary guest apk resolves and verifies an
offline original repository, rather than explicitly installing every alternative
provider. The Apple simulator app links successfully after the refresh-target
actor isolation correction; actual Metal/IME acceptance remains required.

In-flight descriptor transfers have an explicit export lease, registered before
sending SCM_RIGHTS. The receiving service retains it until SharedFile import
acknowledges that FD, or until the client disconnects. The original logical EOF,
managed backing and physical budget therefore survive the interval after the
guest closes its last descriptor but before the server imports the FD. Transfer
backpressure is bounded independently of frame leases; no extra pixel lock is
introduced. Tests cover cancelled transfers followed by another export of the
same inode and cancellation on native client disconnect.

A headless production embedding test also drives original wayland-info, the C
multiwindow peer, Weston SHM/editor and vkcube through the same session/transfer/
frame API as UIKit. It retains SHM and GPU frames across VM shutdown, queries
their capabilities afterwards, then releases them. Linux compute regression
continues to match the independent driver, including mapped-memory transfers,
semaphore reuse and asynchronous pipeline callers.

To reproduce the signed offline store and production ABI acceptance on Linux
(with the AArch64 C compiler, Wayland scanner and Vulkan host driver installed):

```sh
bash scripts/build-vulkan-fixtures.sh
python3 scripts/prepare-wayland-guest.py
cargo build -p trames-ffi --release --locked
TRAMES_WAYLAND_STORE=/tmp/trames-wayland-test python3 tests/integration/wayland.py --prepare
```

The target store must be absent for --prepare. Omit that flag to repeat the
client/lifetime checks against the prepared store. This headless test does not
claim to validate Apple drawable pixels or actual iPad keyboard interaction.

The native text context identifies text-input v1 versus v3. v3 surrounding
removal excludes selected text; preedit/commit replaces that selection itself.
UIKit therefore does not send a duplicate deletion for it. v1 retains explicit
cursor-relative byte deletion. An independent original C v3 peer checks preedit
UTF-8 offsets, the originating done serial and the client's surrounding-text
acknowledgement. UIKit checks cover selection direction, replacement without
double deletion and deleting at an interior composition caret. Popup canvas
padding is transparent and empty gaps do not intercept neighboring controls.
