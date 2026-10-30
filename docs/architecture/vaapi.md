# VA-API over VideoToolbox

Status: selected architecture and native feasibility probe, 2026-10-31.
The guest VA driver, video native-call module, and Firefox video frame imports
are **not implemented**. This document records the implementation path shared by
the Mac and iPad products, following the [accelerator design](accelerator.md).

## Architecture

```text
Linux Firefox / FFmpeg / other VA-API clients
  → distribution libva + trames_drv_video.so (guest Linux ELF)
  → existing NativeCall transport
  → shared runtime video backend
  → VideoToolbox → CVPixelBuffer / IOSurface
  → CVMetalTextureCache → native Metal plane textures
  → guest frame FDs → EGL image import → Mesa Zink → Vulkan / Metal
  → existing Wayland window presentation
```

Keep the libva frontend and its driver ABI. Implement `trames_drv_video.so`
against pinned libva headers, initially selecting it with
`LIBVA_DRIVER_NAME=trames`. A library override alone is insufficient:
`vaGetDisplayDRM()` checks the FD's DRM node type, and Firefox opens a render
node through `DMABufDevice` before initializing VA-API. Provide a Trames virtual
render-device identity and the device/GBM discovery operations actually needed
by these clients. This is a user-kernel ABI adapter; it does not require a
Linux kernel, KMS implementation, or hardware DRM command submission.

Put the guest driver, codec adaptation and a new `trames-video` backend in
`trames-runtime`. Use a small shared frame-resource interface for VFS/video/
Vulkan interoperation; keep Apple types behind the Apple backend. The Mac and
iPad products only select capabilities and link the native frameworks. A fourth
repository or a separate video service process is unnecessary.

## Two integration gaps

### Compressed input adaptation

VA-API VLD is a parsed-picture interface. For example, FFmpeg's H.264 VA backend
submits picture parameters, quantization matrices, slice parameters and slice
data. Its `start_frame` does not submit the original SPS/PPS NAL units.
VideoToolbox's H.264 path constructs a CoreMedia format description from
parameter sets and submits complete compressed samples. A direct dispatch of
the VA vtable to VideoToolbox cannot bridge this difference.

Start with progressive H.264 8-bit 4:2:0. Reconstruct the decode-essential
SPS/PPS, preserve or adapt the slice headers as needed, and form length-prefixed
access units with correct RBSP escaping. Infer parameter-set IDs and any
recoverable defaults from slice data and effective VA parameters. In particular,
the original PPS default reference counts are absent from the picture struct;
setting them unconditionally to zero misinterprets some P/B slices. VA metadata
also omits some original SPS information, including POC type-1 cycle offsets.
Unsupported cases need an honest capability/error boundary, rather than invented
headers. Test ordinary Firefox/FFmpeg decode order, B frames, IDR/seek, parameter
set changes and reference reuse before expanding the advertised profile.

VideoToolbox owns its decoder reference state; VA-API exposes explicit reference
surfaces. Arbitrary reference-picture injection or a decoder resumed in the
middle of a GOP is not automatically equivalent. HEVC VPS/SPS/PPS and AV1
sequence/frame/OBU adaptation require separate proofs. Existing upstream codec
parsers/writers may help, but they do not recover fields the client never sends.
A targeted FFmpeg extension preserving original parameter sets is a possible
compatibility escape hatch if reconstruction blocks real streams; it is not a
prerequisite for every Linux client.

Own the small compressed input until the asynchronous decode finishes. One
bounded compressed-data copy avoids retaining mutable guest pointers after the
client can reuse its buffers. It does not require copying decoded pictures.

### Decoded-frame import into Firefox

The deployed Firefox 142 VA path calls:

1. `vaExportSurfaceHandle(DRM_PRIME_2, READ_ONLY | SEPARATE_LAYERS)`;
2. `vaSyncSurface()`;
3. duplicates and transfers the resulting plane FDs to the compositor;
4. creates per-plane `EGL_LINUX_DMA_BUF_EXT` images and GL textures.

The current [DRM-less EGL/Wayland adapter](zink.md) and Vulkan bridge do not yet
provide this resource import path. Vulkan 1.2 YCbCr conversion alone does not
make a VA surface consumable by Firefox. Its existing YUV compositor can consume
separate R8/RG8 planes; P010 needs the corresponding 16-bit plane handling.

Represent exported objects as real guest FD resources retaining an immutable
decoded-frame version. `dup`, fork, `SCM_RIGHTS`, close and outstanding GPU uses
must retain the same allocation. A reused `VASurfaceID` points at its new frame;
old exported FDs keep their previous frame alive. Extend the existing VFS
open-description and FD-transfer ownership path, without exposing a host pointer,
host FD number or globally discoverable IOSurface ID as a guest handle.

Prefer retaining Mesa's standard external-resource import flow and translating
the guest FD to the owned native texture inside the bridge. The remaining work
includes external-memory format/property queries, allocation/bind semantics,
plane offsets/pitches, image views and synchronization. Determine the smallest
EGL/Zink adapter needed with an actual import fixture; merely enumerating
external-memory extensions is insufficient. Use implicit modifiers where valid;
an opaque native texture must not be advertised as linear CPU memory. Firefox's
YUV import omits modifier attributes for `DRM_FORMAT_MOD_INVALID`.

`VK_EXT_metal_objects` already provides a verified host-side texture import
primitive. Native `VK_EXT_external_memory_metal` is also enumerated by the local
driver, but its memory-allocation/bind route has not been tested by this probe.
Keep Metal handles internal to the backend in either case.

## Ownership, memory and scheduling

- Create the texture cache on the same `MTLDevice` as the active Vulkan device.
  A frame owns its `CVPixelBuffer`, plane texture wrappers and GPU leases until
  all consumers finish. Reuse the existing GPU image lease/wakeup principles;
  the current single-RGBA Wayland presenter is not itself an NV12 importer.
- Allocate decoded storage through VideoToolbox's output pool as frames are
  produced. VA surface IDs can initially be logical slots. Avoid a second pool
  of full-size pictures or a copy into each preallocated VA surface. Retained
  decoder references and in-flight compositor frames still consume real memory.
- Submit native operations on the guest's current host thread. Synchronize
  shared context/frame state at object scope. Keep independent sessions
  concurrent; add no global video lock or coordinator thread.
- VideoToolbox callbacks retain completed native frames, publish status, and
  wake existing resumable waits. They never retain a guest byte borrow or write
  guest memory from the callback thread. `vaQuerySurfaceStatus` is nonblocking;
  waits yield the execution slot. No polling service or busy loop is needed.
- Because Firefox exports **before** synchronizing, export itself may need to
  suspend until VideoToolbox has produced the frame. Complete both operations
  through the same frame-completion state; do not require a different client
  call order or copy into a temporary surface to satisfy it.
- CPU image access (`vaGetImage`/mapping) is an explicit readback path. The
  Firefox rendering path keeps NV12/P010 on native resources and performs color
  conversion in the GPU compositor. Preserve color range, primaries, transfer,
  chroma placement and crop metadata.

## Local native evidence

The [standalone probe](../../tests/fixtures/vaapi-videotoolbox-metal.mm) uses
public Apple APIs and the existing patched static MoltenVK archive. It encodes
one synthetic frame to obtain a valid compressed sample, requires hardware
H.264 decoding, checks the actual decoder property, creates both Metal plane
textures, imports/re-exports them as native Vulkan images, and runs a Metal
compute shader against the decoded planes.

Measured on Apple M6, macOS 27.0.1 (26A434), Xcode/SDK 27.2:

| Check | Result |
|---|---|
| `VTIsHardwareDecodeSupported`: H.264 / HEVC / AV1 / VP9 | true / true / true / false |
| Actual H.264 hardware decoder property and decode | pass |
| Decoded pixel format | NV12 video-range, 128 × 96 |
| Luma / chroma dimensions and byte pitches | 128 × 96 / 64 × 48; both pitches 128 |
| Both textures retain the decoded IOSurface and plane identity | pass |
| Vulkan import/re-export returns each exact same Metal texture | pass |
| GPU sample Y / Cb / Cr (expected 96 / 64 / 192) | 96 / 64 / 192 |
| CPU lock or copy of the decoded picture | none |
| iPadOS SDK compile, deployment target 17.4 | pass; syntax only |

This proves the native shared-plane primitive. It does **not** prove guest
DMA-BUF import, Vulkan sampling/layout transitions, full VA decoding, Firefox
playback, HEVC/AV1 decoding, profile/level coverage, throughput or iPad hardware.
Hardware queries are codec-level hints: advertise VA profiles only after actual
session creation and decoding validate the profile, depth and dimensions on the
current device. VP9 remains on the software path on this Mac unless a later
device/session probe establishes hardware support.

Reproduce with the existing prepared Vulkan headers and static MoltenVK archive:

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
# Set these to the include directory and archive produced by prepare-vulkan.py.
export VULKAN_HEADERS=/absolute/path/to/Vulkan-Headers/include
export MVK_ARCHIVE=/absolute/path/to/libMoltenVK.a
xcrun --sdk macosx clang++ -std=c++17 -O2 -Werror \
  -Wno-missing-field-initializers -I "$VULKAN_HEADERS" \
  tests/fixtures/vaapi-videotoolbox-metal.mm "$MVK_ARCHIVE" \
  -framework VideoToolbox -framework CoreMedia -framework CoreVideo \
  -framework IOSurface -framework Metal -framework Foundation \
  -framework QuartzCore -framework CoreGraphics -framework MetalPerformanceShaders \
  -framework AppKit -framework IOKit -o /tmp/trames-vt-probe
/tmp/trames-vt-probe
```

The probe links MoltenVK directly, whose local build does not expose loader-only
portability enumeration. Its blocking completion calls are confined to this
standalone test, not the proposed guest execution path.

## Implementation order and acceptance

| Stage | Deliverable and acceptance |
|---|---|
| 1 | Shared video module + guest libva driver + minimal render-device discovery; `vainfo` reports only validated H.264 VLD capabilities. |
| 2 | H.264 adaptation and asynchronous lifecycle; stock FFmpeg VA decode matches software reference frames, including B frames, seek, resize and cancellation. Readback is acceptable as a correctness oracle. |
| 3 | Frame FD export/transfer and EGL/Zink import; two guest processes sample the same native planes with zero CPU frame copies, correct lifetime and nonblocking waits. |
| 4 | Stock Firefox RDD → compositor playback with its sandbox retained; verify actual hardware decoding, frame drops, A/V sync, seek, window resize and teardown. |
| 5 | HEVC Main/Main10 and AV1 where the device and adapter pass real samples; encoding and video processing follow separately. |

Measure decode submission/callback latency, native wait time, CPU frame-copy
bytes, end-to-end dropped frames, host CPU, and retained frame/pool memory.
A 4K60 NV12 full-frame copy alone moves about 746 MB/s of payload; compressed
input copies are far smaller. Avoiding raw-frame round trips is the main design
win. This research changes no production executable and requires no repeat of
the unchanged CPU/Linux/network/Vulkan regression suites.

## Primary sources

- [Apple VideoToolbox hardware decoder query](https://developer.apple.com/documentation/videotoolbox/vtishardwaredecodesupported(_:)) and [actual hardware decoder property](https://developer.apple.com/documentation/videotoolbox/kvtdecompressionpropertykey_usinghardwareacceleratedvideodecoder).
- [Apple CoreVideo Metal texture cache](https://developer.apple.com/documentation/CoreVideo/cvmetaltexturecache-q3j).
- [libva 2.22 driver ABI](https://github.com/intel/libva/blob/2.22.0/va/va_backend.h), [DRM display creation](https://github.com/intel/libva/blob/2.22.0/va/drm/va_drm.c), and [PRIME surface descriptor](https://github.com/intel/libva/blob/2.22.0/va/va_drmcommon.h).
- [FFmpeg 7.1 VA H.264 input](https://github.com/FFmpeg/FFmpeg/blob/n7.1/libavcodec/vaapi_h264.c) and [VideoToolbox input](https://github.com/FFmpeg/FFmpeg/blob/n7.1/libavcodec/videotoolbox.c).
- [Firefox 142 VA export/sync](https://github.com/mozilla-firefox/firefox/blob/FIREFOX_142_0_RELEASE/dom/media/platforms/ffmpeg/FFmpegVideoDecoder.cpp), [display initialization](https://github.com/mozilla-firefox/firefox/blob/FIREFOX_142_0_RELEASE/dom/media/platforms/ffmpeg/VALibWrapper.cpp), and [plane import](https://github.com/mozilla-firefox/firefox/blob/FIREFOX_142_0_RELEASE/widget/gtk/DMABufSurface.cpp).
- [Khronos native Metal texture import](https://docs.vulkan.org/refpages/latest/refpages/source/VkImportMetalTextureInfoEXT.html) and [Metal external-memory import](https://docs.vulkan.org/refpages/latest/refpages/source/VkImportMemoryMetalHandleInfoEXT.html).
