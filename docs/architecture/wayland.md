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

Vulkan must use VK_KHR_wayland_surface and the existing standard guest Vulkan
loader/ICD, with authenticated connection/object IDs rather than guest pointers
as native capabilities. GPU presentation must remain native (no normal pixel
readback). The intended compositor path uses retained native Metal resources
and explicit completion/lifetime ownership. D16 depth attachments/testing and
sRGB textures are part of the original vkcube acceptance scope.

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
- The same ARM64 client and original Alpine libwayland-client/libffi execute
  through the Linux guest VFS bridge successfully.
- Rust core/bridge/FFI compile for aarch64-apple-ios. Swift parser checks pass;
  native Apple app build and actual Metal/IME runtime acceptance remain pending.
- Subsurfaces, complete popup policy, focused input protocol acceptance, Wayland
  Vulkan WSI, original wayland-info/Weston applications/vkcube, and Apple UI tests
  are still being completed. This branch is not a completed release.

Upstream wayland-backend is retained under vendor/ with its MIT license and
three documented Apple-platform cfg changes. See TRAMES-PATCHES.md there.
