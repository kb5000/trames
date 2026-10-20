# Xwayland: original guest server, native window management

The first X11 milestone runs the original Alpine ARM64 Xwayland in rootless
software mode. A native Rust XWM uses the pure-Rust x11rb client. Each mapped X11
toplevel joins the existing Wayland window snapshots, frames and workspace UI;
there is no nested desktop canvas or guest compositor/window manager.

The GPLv3 launcher `trames-xwayland` connects the private runtime XWM socket and
execs the original `Xwayland -rootless -shm -nolisten tcp -wm FD`. Its PID and
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
4 KiB per property reply. A full update queue disconnects that XWM without
blocking compositor teardown. Untrusted transient/override-redirect parentage
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
EWMH, selection/XIM and arbitrary legacy desktop behavior are future work.

SHM pixmaps use the same managed FD, scene lease and Metal presentation route
as native Wayland applications. No new CPU pixel-copy bridge is introduced.
This does not establish Apple bytesNoCopy success for Xwayland; the first gate
uses Linux retained SHM frames. Native Wayland Vulkan remains independent.
GLX, DRI3/glamor and X11 Vulkan WSI have **not** passed an acceleration gate.

## Session startup

The next built App includes the small launcher as GuestVulkan data and installs
it at `/usr/bin/trames-xwayland`, independently of enabling Vulkan. Xwayland and
clients remain ordinary signed Alpine packages. Source changes are not an IPA
release; existing downloaded packages do not contain this feature yet.

```sh
apk add xwayland xkeyboard-config xterm xclock xprop font-dejavu
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
