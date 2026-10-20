# Xwayland: original guest server, native window management

The initial X11 milestone runs the original Alpine ARM64 Xwayland in rootless
SHM mode. The default native-WSI transport now adds original glamor/Zink GPU
rendering without a DRM device. A native Rust XWM uses the pure-Rust x11rb client. Each mapped X11
toplevel joins the existing Wayland window snapshots, frames and workspace UI;
there is no nested desktop canvas or guest compositor/window manager.

The GPLv3 launcher `trames-xwayland` connects the private runtime XWM socket and
execs the bundled native-WSI Xwayland with `-rootless -glamor es -nolisten tcp -wm FD`
when this session authorizes Vulkan. `TRAMES_XWAYLAND_NATIVE_WSI=0` or disabling
Vulkan selects the original signed `Xwayland -shm`. Its PID and
credentials remain unchanged. The VFS bridges bytes/owned descriptors to a native
XWM thread, using the existing bounded Unix-stream adapter. Blocking X11 setup
and property replies never run on the compositor, CPU workers or UIKit.

Only a process with a live launcher registration and matching guest PID/UID/GID
can see/bind `xwayland_shell_v1`. The standard committed 64-bit monotonic serial
is matched against `WL_SURFACE_SERIAL`; associations never use recycled Wayland
object IDs. Either protocol stream may arrive first. Unmap/remap retires the old
surface; XWM disconnect revokes registration and invalidates its surfaces.
Window metadata/properties, bridge queues and surface counts remain bounded:
4 XWM registrations, 256 X11 windows per XWM, 256 total Wayland surfaces, and
4 KiB per metadata property reply. Selection/XIM bounds are listed below.
Bounded update queues do not block compositor teardown.
Untrusted transient/override-redirect parentage
cannot introduce scene cycles.

The XWM owns Composite manual redirection, `WM_S0` and `_NET_WM_CM_S0`. It adopts
existing mapped windows as well as future MapRequests. Titles, classes, transient
parents and minimum/maximum sizes feed native snapshots. Resize configures the
X11 window; close uses advertised WM_DELETE_WINDOW or XKillClient. WM_TAKE_FOCUS
is sent only if advertised: sending it to xterm unconditionally can invoke its
generic WM_PROTOCOLS close action.

Wayland and X11 focus requests use independent streams. A native X11 reply
confirms focus before queued keyboard events are released. Focus generations
prevent an earlier A→B→A acknowledgement unlocking a later gesture. This does
not pause the compositor or other clients. Known transient menus/tooltips join
their parent's scene; ordinary dialogs remain separate windows. Full ICCCM,
EWMH and arbitrary legacy desktop behavior remain partial; text selection/XIM
support is described below.

SHM pixmaps use the same managed FD, scene lease and Metal presentation route
as native Wayland applications. No new CPU pixel-copy bridge is introduced.
This does not establish Apple bytesNoCopy success for Xwayland; the first gate
uses Linux retained SHM frames. Native Wayland Vulkan remains independent.
The GPU transport below has passed a Linux Vulkan-reference gate.
GLX, DRI3 direct rendering and X11 Vulkan WSI have **not** passed an acceleration gate.

## Original glamor/Zink with native Wayland WSI

`accelerator/xwayland/native-wsi.c` and the small upstream patch add the default
GPU route. The launcher sets its internal `TRAMES_XWAYLAND_NATIVE_WSI=1` option.
Xwayland 24.1.9's X11 server, glamor drawing
and shaders remain upstream. Mesa 25.1.9's original signed Gallium/Zink, GLSL/NIR
and SPIR-V compilers remain unchanged. The already existing EGL Kopper adapter
connects their rendering to Trames Vulkan, MoltenVK/Metal and native Wayland
frames. This route requires no emulated DRM render node, GBM buffer or DMA-BUF.

Original glamor renders ordinary 24/32-bit X11 pixmaps into textures. A separate
shared ES2 context draws one textured quad into each window's EGL swapchain.
It flips glamor's top-row-at-v=0 texture coordinates for Wayland's displayed
top-left origin and forces opaque alpha for depth 24. Shared texture parameters
are restored, and EGL GPU fence/wait pairs order both contexts. There is no
production pixel readback, CPU staging pixmap or glFinish. Presentation includes
one GPU draw/copy; it is not direct scanout of the original glamor texture.
Original glamor still uses its own software fallbacks for unsupported drawing
operations and depths. Those do not switch ordinary window presentation to SHM.

EGL surfaces track actual X11 buffer dimensions and are destroyed before their
Wayland surfaces. Existing Vulkan/Wayland frame leases retain old native images
over resize, window destruction and session shutdown. Native mode requires an
actual Zink renderer plus real fence/wait extensions. Initialization/presentation
failure is reported; it does not silently select SHM. The launcher keeps the
original `-shm` route for an explicit software override or disabled Vulkan.

The Vulkan context now permits 16,384 retained objects, rather than the initial
256-object smoke-test limit. Original glamor's descriptors and pixmap/shader
caches exceed 256 even with xterm/xclock. Allocation bytes still use the existing
GPU budget, and other bounds remain. Sampler descriptor updates preserve known
native attachment/transfer layout enums used by Zink for unaccessed slots;
they do not require shader-read layout before actual use. Native object identity,
usage checks, dependencies and in-flight retention remain intact.

Build with `python3 scripts/build-xwayland-native.py` after preparing the normal
Mesa EGL prerequisites. It needs AArch64 GCC/binutils, native Meson/Ninja,
pkg-config, bison/flex and wayland-scanner. The pinned original Xwayland archive
is SHA-256 verified. Output includes a binary/hash manifest, original notices,
GPLv3 terms, and `target/xwayland-native/xwayland-native-source.tar.gz` with the
original source archive, patch, adapter and build inputs. The iOS prerequisite
job carries the source bundle alongside the guest artifacts. The test-only
pixel interposer stays outside `GuestVulkan` and the App.

With Vulkan enabled and the bundled EGL adapter present, the session validates
and atomically installs the sibling bundled server at
`/usr/libexec/trames/Xwayland-native`. It preserves the apk-owned Xwayland.
The App defaults Vulkan to enabled for a previously unset preference; an
existing explicit disabled preference is preserved. Apple hardware acceptance
is still pending; this does not gate the owner's requested default:

```sh
apk add xwayland xkeyboard-config xterm xclock font-dejavu \
    mesa-dri-gallium mesa-egl mesa-gles vulkan-loader wayland-libs-egl
trames-xwayland :0 >/tmp/xwayland.log 2>&1 &
export DISPLAY=:0
xterm &
xclock &
```

This enables accelerated X11 server drawing/presentation. The bundled server
is built with GLX disabled; accelerated OpenGL clients via GLX/DRI3, X11 Vulkan
WSI require separate implementation and acceptance.

## Text clipboard and native input methods

The native XWM bridges ICCCM `CLIPBOARD` to the existing Wayland data-device
and native `HostClipboard` provider. X11 Copy, including menu Copy with no key
serial, observes XFixes selection ownership and fetches text from its real
owner. UTF8_STRING, TEXT, Latin-1 STRING, TARGETS and TIMESTAMP are supported;
incoming INCR chunks share the existing 16 KiB text bound. A transfer owns its
own request window, times out and is cancelled on owner replacement. PRIMARY
remains X11-local, so selecting text does not overwrite the iPad clipboard.

X11 paste requests are served lazily from the active Wayland source, owned X11
text or the native provider. The normal iPad paste-permission dialog therefore
appears on an actual host read. There is no extra Trames confirmation. Pending
reads never block the compositor/CPU threads; held keys are released before a
permission handoff. Selection generations and serials reject stale offers and
older owner updates. Copied X11 bytes survive their source's exit. Native
transfers retain the original provider's 16 KiB limit and four-read bound;
cached-text pipe writers are capped at eight and request windows at four.

The original MIT `xim-rs` 0.5.0 server, parser and Compound Text codec serve
`@im=trames` over the existing pure-Rust x11rb connection. New local terminals
set `XMODIFIERS=@im=trames`. Standard Xlib input contexts receive confirmed
Unicode via XIM_COMMIT, including non-BMP characters. UIKit owns composition
and candidates. This first adapter advertises root-style preedit/status-nothing
contexts; full client-rendered preedit styles and surrounding-text editing are
future work. UIKit edits existing client text with ordinary guest editing keys.
Caret offsets are translated from the XIM focus window to the native toplevel.

XIM resets, IC destruction and native focus changes retire the old text epoch.
Delayed native compositions cannot enter another field/window. The XIM wrapper
bounds transports to 64, live ICs to 256, commits to 4 KiB, and property reads
to 64 KiB. Destroyed clients are removed without stopping unrelated windows.
The XIM protocol runs off the compositor thread. Original library notices and
the full locked dependency notice closure accompany the GPLv3 application.

X11 keyboard events use the original server's XTEST extension on the same
connection as XIM commits. Unicode commits, navigation and following text retain
their order without an extra cross-thread acknowledgement per keystroke. The
native XWM tracks/reconciles modifiers and releases held keys on focus loss;
native Wayland keyboard delivery is unchanged.

## Session startup

The next built App includes the small launcher as GuestVulkan data and installs
it at `/usr/bin/trames-xwayland`, independently of enabling Vulkan. Xwayland and
clients remain ordinary signed Alpine packages. Source changes are not an IPA
release; existing downloaded packages do not contain this feature yet.

```sh
apk add xwayland xkeyboard-config xterm xclock xprop font-dejavu \
    mesa-dri-gallium mesa-egl mesa-gles vulkan-loader wayland-libs-egl
trames-xwayland :0 >/tmp/xwayland.log 2>&1 &
export DISPLAY=:0
while ! xprop -root >/dev/null 2>&1; do sleep 0.1; done
xterm -fa 'DejaVu Sans Mono' -fs 12 &
xclock &
```

The X11 server is started explicitly. This is not automatic socket activation.
On first native-display startup, only persisted X11 sockets with no live VFS
endpoint and their ordinary lock files are cleared. Live sockets and user files
are preserved. Checking only the old lock PID is insufficient after the virtual
kernel restarts its PID allocation.

## Signal compatibility

Original Pixman leaf assembly can temporarily save an 8-byte register on SP.
An asynchronous signal may interrupt that non-call-boundary SP. Trames aligns
the new handler frame but must restore the original interrupted SP. Requiring
16-byte alignment in rt_sigreturn rejected this legitimate frame and let musl's
restorer fall through into sigsetjmp after an erroneous EFAULT.

The fix removes that extra interrupted-SP alignment check. Frame bounds, guest
address range, saved PC validation, FPSIMD context and alternate-stack validation
remain. Linux v6.12 `arch/arm64/kernel/ptrace.c:valid_native_regs`, used by signal
restoration, validates user PSTATE without imposing this SP alignment test.
The ARM64 fixture `tests/fixtures/signal-leaf-stack.c` raises SIGUSR1 while SP is
temporarily 8-byte aligned and checks successful return and exact SP recovery.

See [validation](../validation/xwayland.md) for the measured first milestone.
