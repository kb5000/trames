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

## DRM-less glamor/Zink transport

2026-10-21, same Linux host/ARM64 guest, four workers, 4 GiB aggregate and
per-process commitment limits. The production FFI installs the bundled original
Xwayland 24.1.9 plus GPLv3 transport, alongside original Alpine Mesa 25.1.9,
libwayland, xterm and xclock. The native Vulkan reference is Lavapipe on a CPU;
this establishes protocol/rendering correctness, not iPad GPU performance.

The native-mode gate checks:

- Actual Zink renderer, EGL fence/wait support and GPU frame type 2 throughout;
  no SHM presentation fallback.
- Independent xterm and xclock windows, 480×320 terminal resize, focus plus
  immediate `a`/Return, exact ordinary child-shell output, and independent close.
- An ordinary Xlib client fills a pixmap, copies it into its window and uses
  XGetImage to check all 12,288 pixels at 128×96 and 16,000 at 160×100.
- A **test-only** EGL dispatch interposer checks the same pixels and orientation
  in the final presentation framebuffer, then forwards to original Mesa's swap.
  The exact production Xwayland ELF is used; the oracle is not App code.
- Old GPU frame metadata remains valid across resize/close and session shutdown.
  Apple mode additionally requires the exported Metal texture pixel oracle,
  including the retained old texture. This Apple gate has not run here.

The initial object bound rejected normal descriptor allocations; increasing it
to 16,384 resolved that limit. Further checks found and fixed the vertical
texture-coordinate conversion and premature rejection of Zink's unaccessed
sampler-slot attachment layouts. Unsupported 8/15/16/30-bit glamor formats can
still report original per-format software fallbacks; ordinary depth-24/32
presentation stays on the GPU transport. This does not establish full GLX or
arbitrary application's OpenGL support.

Scoped checks also pass: 18 Vulkan unit tests, release Clippy for Vulkan/distro/
FFI, Rust formatting, Python parsing and shell syntax. The changed launcher and
installer also pass the original SHM gate on reopened storage. Unchanged CPU,
Linux, networking and other graphical suites are reused.

The original-source transport ELF SHA-256 is
`d26827c2a162d8859cfa6467d6ad1eb979754efbf205f1f9afd9275e0d859288`;
the production Linux FFI SHA-256 is
`5689bdfba88055801ba391be91b26e8943e9c50431a480a3d857c3afaf5e8f3c`.

Reproduce after building the native transport and installing original signed
Xwayland/Mesa packages into an isolated store:

```sh
# CPU reference only. For Apple, select the native Metal backend instead.
TRAMES_ZINK_CPU_REFERENCE=1 python3 tests/integration/xwayland.py \
    --store STORE --output NEW_DIRECTORY --native-wsi
```

Select the host reference ICD with `VK_DRIVER_FILES` and, where necessary,
`TRAMES_VULKAN_LIBRARY`. On Apple, provide `--metal-validator` built from
`tests/fixtures/zink-metal-pixels.m`. Raw guest/host logs stay local. No new IPA
or hardware GPU acceptance is implied by this source milestone.

## Default GPU, text selections and XIM

2026-10-21: the launcher's default now selects the bundled glamor/Zink server
when the session authorizes Vulkan. The App's previously unset GPU preference
also defaults to enabled, preserving an explicit disabled setting. The final
production-FFI run passed without setting `TRAMES_XWAYLAND_NATIVE_WSI=1`.
`TRAMES_XWAYLAND_NATIVE_WSI=0` keeps the original SHM route available.

The independent original-Xlib fixture and signed mousepad 0.6.3-r1 passed:

- XOpenIM/XCreateIC and native-confirmed Chinese plus emoji, with an immediate
  Left key followed by more text; the client records the exact event order.
- A preedit update does not commit text. Resetting the same IC changes its
  epoch; late text after IC reset or native window focus change is rejected.
- Ctrl+C and right-button menu Copy produce exact UTF-8 host clipboard writes.
- A forced ICCCM INCR transfer copies 9,000 bytes of Chinese text correctly.
- Host paste is read on demand. Holding the paste key for over 800 ms while the
  mock native permission reply is pending creates exactly one read request.
- Wayland mousepad Copy pastes into X11; X11 Copy pastes into mousepad, with no
  extra host clipboard reads. The saved editor document is verified directly:
  GTK's surrounding-text snapshot can lag an asynchronous paste.
- Existing window/input/close/retained-frame and GPU pixel checks pass with
  the new XWM services. The explicit SHM path also passes interaction checks.

The original Wayland mousepad Ctrl+C, menu Copy, navigation and subsequent IME
regression gate passes too. Ten Wayland unit tests and scoped release Clippy,
Rust formatting, Python/shell checks and original dependency-notice verification
pass. Unchanged CPU/Linux/network/Vulkan unit suites reuse their prior results.
Two UIKit tests cover XIM composition/caret refresh, physical deletion and
confirmation-before-navigation; no Mac/iPad is attached, so those new UIKit
tests and the real system paste dialog/keyboard still need Apple validation.

XIM protocol/Compound Text behavior reuses upstream MIT xim-rs 0.5.0,
xim-parser 0.2.2 and xim-ctext 0.4.1. The latter two crates omit their workspace
LICENSE; the committed original license is identical in their exact upstream
commits `7300de35a111de2df1a694bf70454532a8419685` and
`c4b897800137f022fbf617cd1e81756c6f3bc955` (SHA-256
`efa1224931b98468f5005f77285457f8d2eab0a77a15941aa10abbab1ae9a74e`).
Original notices retain their original line endings.

Reproduction adds `--interactions --wayland-peer` to the native-WSI command
above, after installing the ordinary signed `mousepad` and `font-dejavu` packages.
The final run took about 32 seconds; it is a functional gate, not a benchmark.
Production Linux FFI SHA-256:
`1dff200d6fe21ff8fecfe18b54f4178678f866b09ec7ae0b568230bcb9588c4e`;
default-GPU launcher SHA-256:
`a4defe9c381f48e5c97c890f5e113dea3386c3b9b2acf45cb03f3f724d65c07c`.
The original glamor/Zink guest server ELF is unchanged from the previous gate.
No IPA or raw diagnostic bundle was published in this iteration.

Reproduction inputs: `scripts/prepare-xwayland-guest.py` downloads original
signed APKs and builds the launcher/signal fixture. Import its repository into a
fresh isolated store using `trames-run --file`, then install with ordinary guest
`apk --repositories-file` (see existing developer/Wayland preparation workflows).
Run `python3 tests/integration/xwayland.py --store STORE --output NEW_DIRECTORY`.
`--wayland-debug` and `--capture` are optional diagnostic modes; the passing
final run used image capture, without verbose Wayland logging.
