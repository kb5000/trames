# Xwayland first milestone

2026-10-21 (Asia/Shanghai), Linux x86_64 host, ARM64 guest, four CPU workers,
2 GiB aggregate/per-process guest commitment limit, network off, original hybrid
musl and original Alpine-signed packages. No Firefox preferences or binaries were
modified; its default-refresh investigation is paused at the owner's request.

Original Xwayland 24.1.9-r0, xterm 399-r0 and xclock 1.1.1-r0 passed through the
production session/Wayland FFI, with native Rust window management:

- Two separately titled native snapshots: 160×160 clock and 244×134 terminal.
- Retained SHM terminal pixels contain the ordinary child shell's text.
- Host resize produces an actual 480×320 X11 window/buffer.
- Focus and `a`, Return submitted immediately, without a focus-settling sleep;
  the ordinary xterm child shell records exactly `X11_KEY:a`.
- Native close removes the terminal while the clock survives, then closes the
  clock. XWM/server shutdown leaves no visible windows.
- A retained frame remains valid after session shutdown and releases cleanly.
- Ordinary guest SIGUSR1 returns through a temporary 8-byte-aligned leaf SP.
- Reopened storage starts without stale X11 socket/lock collisions. The bundled
  launcher installs on first use and identical bytes are reused on reopening.

The final probe took about 4 seconds including setup and diagnostic export.
This is functional acceptance, not a performance score. Intermediate runs found
and fixed interrupted-SP validation, undeclared WM_TAKE_FOCUS, independent-stream
focus ordering and stale locks. Export checks wait for complete shell output;
seeing the file after redirection creates it does not mean printf has finished.

Scoped checks: 10 Wayland unit tests, 11 core signal tests, 3 native-bridge VFS
tests, strict release Clippy for Wayland/VFS/distro/core/FFI, Rust formatting,
Python parsing and shell syntax checks. Socket tests require the execution
environment's Unix/network permission; an unpermitted invocation was terminated
and the affected tests passed with permission. Unchanged CPU, MMU, Vulkan and
full application suites were reused. QEMU is absent here; the guest signal
fixture and independent Linux kernel source comparison are not a native ARM
hardware oracle.

This gate does not validate iPad presentation, 16 KiB Apple pages, native IME/XIM,
X11↔Wayland/iPad clipboard, GLX/DRI3/glamor, X11 Vulkan WSI, complete ICCCM/EWMH,
or all legacy application menus. Those remain follow-up acceptance. No IPA was
built or released for this milestone. Raw diagnostics stay outside the commit.

Reproduction inputs: `scripts/prepare-xwayland-guest.py` downloads original
signed APKs and builds the launcher/signal fixture. Import its repository into a
fresh isolated store using `trames-run --file`, then install with ordinary guest
`apk --repositories-file` (see existing developer/Wayland preparation workflows).
Run `python3 tests/integration/xwayland.py --store STORE --output NEW_DIRECTORY`.
`--wayland-debug` and `--capture` are optional diagnostic modes; the passing
final run used image capture, without verbose Wayland logging.
