# Workspace, input focus and vkmark acceptance — 2026-10-17

Implementation: `fc4ff5237cc1ff3a1ba5c79c1b46dbe580f3cdd3`.
[Apple workflow 38999206546](https://github.com/kb5000/trames/actions/runs/38999206546)
passes **33 actual simulator tests, zero failures and zero skips**: one peer-tab
and floating-window gesture case, followed by 29 app tests and three other UI
cases. Both simulator and ARM64 Release device apps link. The runtime is an
ARM64 iPad Pro 11-inch (M4) simulator running iOS 18.5. Raw reports:
[remaining 32 tests](wayland-workspace-vkmark-simulator.json) and
[workspace gestures](wayland-workspace-vkmark-gestures.json).

Terminal and Wayland surfaces share the root tab/window model. Reordering,
detach/dock, minimize/restore and close preserve identity and owning sessions.
The old display-monitor menu, floating canvas and automatic native display
attachment are removed; its regression probes live only in XCTest.
Empty OSC titles retain Alpine. First responder follows focus changes instead
of frame polling; inactive canvases cannot reload another window's keyboard.
Host title controls stay above client-side shadow/popup extents. v1 activation
is exclusive and duplicate activation does not increase the enter/leave count.

Local validation passes 138 affected library tests, five original C Wayland
acceptance groups, six portable workspace tests, strict Clippy and the Rust iOS
target check. Production FFI runs original wayland-info, two-window C, Weston
SHM/editor and vkcube clients, retaining SHM/GPU capabilities across VM shutdown.
The original editor confirms A → B → A with independent surrounding text:
`Entry你好🙂`, `Numeric第二框`, then `Entry你好🙂返回`.

Original signed Alpine vkmark 2025.01-r0 passes all 13 default scene variants at
320x240 with short 0.05-second functional runs. Scene exceptions fail acceptance
even if vkmark exits zero. This is not a performance measurement. Its requested
sampler anisotropy uses native feature/limit negotiation. D32 depth, Mailbox,
static viewport/scissor, color/alpha blending and transfer color clears execute
through the native driver. Native and one/four-CPU guest pixel outputs match
byte-for-byte for basic, base-offset, anisotropy/clear and static alpha-blend modes.
The Linux native oracle is lavapipe. Actual Apple Metal passes original vkcube
and three vkmark scenes (clear, anisotropic texture and Phong), plus native
anisotropy/transfer-clear and static source-alpha blend pixel checks. Signed
offline APK setup takes about 189 seconds; all six Wayland client stages finish
in about 203 seconds. Original Weston completes the widget return and native
Chinese/emoji composition checks. SHM fallback copies remain zero.

The UI test harness selects the existing DEBUG steady cursor, since indefinite
cursor blinking makes XCTest wait a minute at each action. Window/UIKit
animations remain enabled. The shadow-bearing workspace gesture case runs first;
remaining tests run once afterwards even if the gesture fails. Both actual
reports must pass before building the device App. Concrete controls own their
accessibility IDs; container IDs would overwrite descendants in SwiftUI.
Raw in-progress test diagnostics survive a workflow timeout.

Reproduce using the commands in [the Wayland architecture](../architecture/wayland.md).
The existing 2026-10-16 acceptance record remains historical. This update is not
full Wayland/Vulkan conformance; physical iPad gestures and system candidate UI
still require device use. Normal presentation has no pixel readback; test-only
Metal capture and independent offscreen readback validate actual pixels.
