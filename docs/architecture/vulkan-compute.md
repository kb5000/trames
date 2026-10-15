# Vulkan compute and offscreen graphics accelerator

Trames implements an experimental Vulkan compute/offscreen graphics profile. An original
Linux application uses unmodified `libvulkan.so.1`; its loader discovers the
guest ELF ICD, which bridges to native Vulkan. iPad device/simulator builds
statically link original public-API MoltenVK 1.4.2 over Metal. An iPad preview
displays the actual rendered pixels; window-system surfaces/swapchains and input
follow this offscreen stage. Host port forwarding is deferred by the owner. Tracker:
[Issue #21](https://github.com/kb5000/trames/issues/21).

This selected subset is not complete Vulkan 1.0 conformance. Loader/ICD version
negotiation describes the base ABI; it does not advertise the host's entire API.

```mermaid
flowchart LR
    A[Linux ARM64 application] --> L[Original Vulkan loader]
    L --> I[Guest compute ICD]
    I --> N[NativeCall module 6]
    N --> R[Scoped resources and memory translation]
    R --> M[Native MoltenVK]
    M --> G[Metal GPU]
```

## ABI and activation

The existing `HLT #0x5452` transport negotiates a separate auxv key `0x54520008`,
profile `0x10006`, function 1 and 128-byte little-endian request records. The
frontend explicitly flattens arrays and structures. Guest pointers, native
function pointers, Objective-C objects and native handles are never passed
through unchanged. Proc-address queries return ordinary guest ELF functions.
Dispatchable objects contain the loader's required first word plus an opaque
resource ID. Loader-private create-info nodes are recognized locally; unsupported
application `pNext`, allocation callbacks, extensions and flags are rejected.

The CPU gates Vulkan independently of pure-memory NativeCall. The coordinator
checks its host-created capability snapshot and exact stub code before invoking
the backend. Matching instruction bytes alone do not grant GPU access. CLI
activation is `--vulkan`. iPad's default-off “Vulkan 计算与图形（实验）” switch applies
to the next local session, which receives the verified ICD at
`/usr/lib/libtrames_vulkan_icd.so` and its JSON under `/usr/share/vulkan/icd.d`.
Install the ordinary `vulkan-loader` package with apk.

## Supported compute operations

The [catalog](../../accelerator/vulkan/entrypoints.inc) contains 85 public
functions plus ICD negotiation exports. The profile supports one instance,
compute-capable physical device, logical device and queue per address space;
property queries; buffers and memory; SPIR-V modules; storage/uniform buffer
descriptors; pipeline layouts and one compute pipeline per create call; primary
command buffers, dispatch, push constants, barriers, buffer copy/fill; fences,
binary semaphores, resets and idle waits. Compute pipeline specialization copies
up to 64 map entries and 64 KiB of constant data into owned host records, checks
IDs/offsets/sizes, and passes them to the original driver/compiler.

Shader ingress removes only SPIR-V `OpName`/`OpMemberName` debug annotations.
MoltenVK 1.4.2's original SPIRV-Cross can otherwise preserve a valid GLSL variable
such as `bias` and collide with `metal::bias` during MSL compilation. Header IDs,
entrypoint strings, decorations/specialization IDs and executable instructions
are retained verbatim; original native compiler algorithms remain unchanged.

Queue submission preserves every `VkSubmitInfo` boundary and wait-stage mask.
One call supports up to 64 records and 64 waits, command buffers and signals in
each category across the batch. The original driver performs semaphore waits and
signals asynchronously. With the single-queue profile, a binary wait consumes a
previously queued signal. Validation changes temporary state only; a rejected
batch does not consume signals, mark commands pending or submit earlier records.
Semaphore-only submissions and reuse after a consumed signal are supported.
Timeline semaphore extensions remain unavailable.

## Offscreen graphics and visible preview

Core graphics now includes native 2D optimal-tiled RGBA8/BGRA8 UNORM images,
binding/memory requirements, color image views, a single-color render pass and
framebuffer, vertex/fragment graphics pipelines, dynamic viewport/scissor, draw,
image layout barriers and image-to-buffer copy. Queue queries retain native
graphics/compute/transfer flags. Image format queries expose only this profile:
one mip/layer/sample and dimensions up to 4096; allocation/session budgets still
apply. Device memory remains explicit and noncoherent at the guest boundary.

The first pipeline profile generates vertices in the vertex shader, uses triangle
list/strip topology and an empty graphics layout. Vertex/index buffers, graphics
descriptors/textures, depth/stencil, blending, multisampling and graphics
specialization are not implemented. Dynamic viewport/scissor must both be set;
draw checks recording/render-pass/pipeline compatibility. Buffer transfers remain
outside render passes. Framebuffers/views/images retain their memory and render
pass; recorded commands and in-flight snapshots retain the complete graph.
Graphics compilation uses the same owned, bounded native tasks and fallible
output rollback as compute. No guest pointer or Rc crosses native worker threads.

The iPad session menu's “GPU 渲染示例” opens a temporary offline Alpine environment,
installs the bundled original signed Vulkan loader and runs the same ARM64 C
program as the native/guest reference. Original MoltenVK compiles the vertex and
fragment SPIR-V and Metal rasterizes the triangle. After fence completion, a
native image-to-buffer copy and explicit invalidate populate guest memory. The
guest independently checks barycentric colors, alpha, background and scissoring,
then writes a standard P6 PPM. Existing coordinated file transfer exports that
image; CoreGraphics/UIKit display the bounded pixels without drawing the scene.
Closing the preview cancels pending work and closes/deletes the temporary
environment. This is an end-to-end offscreen preview, not a live guest window or
swapchain; it has readback/file-transfer costs and does not claim zero-copy.

Optional features are zero and extensions are not advertised. Unsupported entrypoints
return NULL. Unsupported creation parameters return Vulkan failures. Recording
functions have void signatures; invalid/unsupported recording parameters abort
the experimental guest ICD with a diagnostic rather than fake success.

Limits include 256 retained native objects per context, 64 in-flight submissions,
64 MiB per native memory allocation and 128 MiB shared across the session's
contexts. Native driver/pipeline bookkeeping is additional memory; these logical
budgets do not establish an exact RSS or physical GPU capacity.

iOS simulator instances use MoltenVK's original direct resource-binding path
through per-instance `VK_EXT_layer_settings`. The iOS 18.5 `SimMetalHost` aborts
inside argument-encoder buffer deserialization during descriptor allocation;
`MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=false` avoids that simulator path while
executing the same Metal workload. No process-wide environment mutation or
software GPU replacement is used.

## Memory and lifetime

Mapped memory currently has separate native and guest storage. `vkMapMemory`
returns a guest anonymous mapping. Flush/invalidate use the interpreter's scoped
page access to copy directly between each guest page fragment and retained native
storage. The full-range intermediate Vec is removed: one copy replaces two for
resident mappings. Shared/live backing providers retain their original direct
borrow or bounded page fallback, including COW, permission and code revision
behavior. Guest and GPU storage remain separate.
Bounds, mapping identity and noncoherent-atom alignment are checked. The guest
mapping aligns `pData-offset` to the native `minMemoryMapAlignment`, including
unaligned mapping offsets; the underlying anonymous mapping is retained
separately for unmap/free. Partial flush/invalidate translates allocation offsets
to mapping-relative guest addresses. HOST_COHERENT
is cleared even when the host type has it: applications must follow the reported
properties and explicitly flush/invalidate. Shared storage remains future work.

No guest borrow survives a submission or wait. GPU buffers live on the host.
Fence polling is nonblocking; the guest wait wrapper uses monotonic deadlines
and nanosleep so other virtual threads can run. Queue idle appends a fence after
prior work. Lightweight creation and recording still execute at coordinator
checkpoints. Compute and graphics pipeline compilation execute on owned native worker tasks.

The Vulkan C API remains synchronous for its caller: the interpreter suspends
only that guest thread until the native result arrives. Other guest threads and
unrelated compiler tasks continue. Native workers receive owned entrypoint names,
specialization bytes, native handles and Arc-owned driver lifetimes; no guest
address, memory guard or coordinator Rc crosses threads. The coordinator retains
the shader/layout and reserves an object slot until completion. A shared session
admission counter permits eight concurrent native compile tasks without a global
compiler work lock; capacity exhaustion returns out-of-host-memory. Polling is
bounded at 1 ms while tasks wait. Driver-internal synchronization stays unchanged.

Completion checks the current guest output mapping before publishing an ID.
A fallible backing write can still fail after preflight; that failure rolls back
the unpublished resource and destroys its native pipeline. Caught guest signal handlers wait for
the active native call to return; stop/continue preserves the waiting call.
Killed callers abandon results, which the coordinator reaps after native work
finishes. Final context teardown joins compiler tasks and GPU work before dropping
dependencies. Final teardown can wait for the original compiler/driver; ordinary
progress polling does not wait for unfinished compilation.

Objects retain required memory, layouts and pools. Recorded commands retain
referenced objects; submissions snapshot their dependency graphs and retain
wait/signal semaphores until native fences finish. Descriptor updates can release old bindings without invalidating
in-flight resources. Destroying an ID removes lookup access while native
dependencies finish. Address spaces have separate, non-reused opaque IDs;
CLONE_VM threads share resources. Fork does not duplicate live native Vulkan
resources: a child initializes its own instance/device. Successful exec and
final address-space exit release old resources; failed exec preserves them.
Final teardown joins native work before freeing dependencies.

## Original source and delivery

Rust uses pinned ash 0.38.0 and matching Vulkan 1.3.281 definitions. Native
MoltenVK algorithms are unchanged. Universal Apple archives are thinned into the
target build directory with original upstream bytes retained. Apple's original
matching compiler-rt provides native platform availability checks. Rust and Xcode
use the app's iOS 17 deployment target and explicit framework dependencies.
No guest CPU JIT is introduced; the original
graphics stack translates SPIR-V and compiles GPU shaders.

[Upstream records](../../scripts/vulkan-upstream.json) pin commits, URLs and
hashes. `prepare-vulkan.py --apple` retains MoltenVK source, every dependency
revision named by that release and original notices. Apple CI uploads them with
the driver artifact. Bridge code remains GPL-3.0-or-later, and upstream notices
remain intact. A future IPA release must pair this repository's corresponding
source with the native-source artifact. Published 0.14.0 stays unchanged.

## Subsequent stages

Expand compute according to actual applications: pipeline caches, more
descriptors and `pNext` records, multiple queues and storage
images. Introduce shared guest/GPU backing with explicit remap, COW and completion
semantics. Extend native task dispatch to other expensive creation operations and
add a reusable compiler worker pool if measurements justify it.

Then verify offscreen graphics before adding presentation. A host-owned surface
token can connect a guest swapchain to an iPad Metal view; a guest address never
becomes a CAMetalLayer pointer. X11/Wayland WSI and a DRM subsystem are separate
compatibility work. See [verification](../validation/vulkan-compute.md).
