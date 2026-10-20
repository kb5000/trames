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

Reproduction inputs: `scripts/prepare-xwayland-guest.py` downloads original
signed APKs and builds the launcher/signal fixture. Import its repository into a
fresh isolated store using `trames-run --file`, then install with ordinary guest
`apk --repositories-file` (see existing developer/Wayland preparation workflows).
Run `python3 tests/integration/xwayland.py --store STORE --output NEW_DIRECTORY`.
`--wayland-debug` and `--capture` are optional diagnostic modes; the passing
final run used image capture, without verbose Wayland logging.
