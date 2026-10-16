# Native Wayland acceptance — 2026-10-16

[Actual Apple run 38963548724](https://github.com/kb5000/trames/actions/runs/38963548724)
passes **23 tests, zero failures and zero skips** (19 app unit/integration tests
and four UI tests). Both simulator and unsigned iPad device applications link.
Runtime source is commit `713ffd93a15193397d0a85abf961bfc68c451326`.
Commit `814744fb3300bc9fe38697f69ee200c21f59a76d` leaves native/test sources
unchanged and adds original dependency notices plus IPA fixture exclusion.
The [machine-readable record](wayland-native.json) identifies test names, original
inputs, assertions and diagnostic-output hashes.

The actual runtime is an ARM64 iPad Pro 11-inch (M4) **simulator**, iOS 18.5.
Original signed Alpine clients run in the guest against real CAMetalLayer
rendering: wayland-info, an independent two-window C peer, weston-simple-shm,
weston-editor and `vkcube --wsi wayland --c 30`. The acceptance requires:

- Configure-driven 320x240 client/image recreation, frames and native close.
- UIKit preedit followed by Chinese/emoji commit, acknowledged in the original
  editor's surrounding text; separate v3 selection, UTF-8 and composition-caret tests.
- Zero SHM fallback-copy bytes on this native device/driver combination.
- Original vkcube native GPU image presentation and more than 64 distinct final
  drawable RGB values after resize. Final-pixel readback is test-only.
- Native Metal compute using persistent coherent mappings without explicit
  flush/invalidate, preserving independent CPU/GPU regions and unmap/remap data.

Linux validation passes 182 affected library tests, strict all-targets Clippy,
four independent original C acceptance groups, and all five original guest
client groups through the production embedding ABI. SHM/GPU frame capabilities
remain queryable after VM shutdown and release cleanly. Compute, coherent memory,
graphics and texture programs match the independent native Vulkan driver with
one and four guest CPUs; graphics/texture pixel outputs match byte-for-byte.
The host timezone/Python regression and Rust iOS target check also pass.
Reproduction commands and exact protocol/resource boundaries are in the
[architecture](../architecture/wayland.md) and [usage guide](../guides/wayland.md).

The coherent fix preserves native HOST_COHERENT memory types and synchronizes
changed guest/native bytes at the profile's submit/fence/unmap boundaries. This
is still a shadow mapping, not a direct CPU alias of Vulkan storage. Presentation
exports the original native MTLTexture and has no normal pixel readback path.

A small host packaging contract check confirms that sideload IPA packaging omits
the 107 MiB DEBUG offline Wayland repository/client fixtures while retaining the
native ICD and notices. A subsequent cloud build covers those packaging/resource
changes separately. Original notices are reproducible with
`python3 scripts/prepare-wayland-notices.py --check` after Cargo fetch/build on Linux.

This is initial feature acceptance, not full Wayland/Vulkan conformance or a
published release. Clipboard/DnD, custom cursor images, reactive popup relayout
and EGL/OpenGL bridging remain outside the initial profile. Real iPad touch,
window gestures and system keyboard/candidate behavior still need device use;
programmatic UIKit composition in a simulator does not establish that experience.
