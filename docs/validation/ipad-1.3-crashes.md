# iPad 1.3.0 build 35: sparse RAM and Xwayland dialog investigation

2026-10-22. The owner's iPad crash is a Rust host allocation abort while constructing
a ConcurrentPage in parallel::prepare_frame, rather than a recorded jetsam kill.
No raw device report, addresses, personal identifiers or Firefox diagnostics are published.

A reproducible memory amplification is fixed: after parallel publication and fork,
untouched anonymous pages may have mapped/shared byte owners but no resident charge.
Hardware host-group relocation treated the owners as live data and copied every
zero page into new physical storage. Treat these still-uncharged anonymous pages as
logical zeros before relocation, preserving quota admission, fork isolation, checked
permissions and existing CPU/native byte ownership. Backed and resident pages retain
their existing handling. This changes mapping/checkpoint work only.

The new regression fails before the fix. After it, the 94 local Memory tests pass.
It checks 16KiB grouping, actual Linux mincore residency (no zero RAM copy), first-write
quota, adjacent pages and both directions of fork isolation. The [Linux/Apple CI](https://github.com/kb5000/trames/actions/runs/39656197539)
passes Memory 94 on both platforms, strict Clippy and iOS FFI; a physical iPad Firefox recheck is still required before claiming the
reported end-to-end crash is completely resolved.

The separate mousepad X11 unsaved-close dialog fails native pipeline creation on the
owner's Apple GPU and subsequently aborts Xwayland. Missing optional feature warnings
alone do not establish its cause. A targeted original mousepad dialog reproduction
and native driver diagnostics are being prepared. Existing passed unrelated graphics,
CPU, Linux, network and frontend checks are reused; the released IPA is unchanged.

## Original mousepad dialog reproduction

The Linux native-WSI case reproduces the failure after typing into the original
signed mousepad and closing its X11 window. The legacy Mesa render-pass builder
uses layout 1000339000 (ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT), despite that
extension not being exposed. Its existing barrier path already selects GENERAL.
The frontend also rejected legal GENERAL and read-only depth attachment references,
and the host previously reconstructed every reference as attachment-optimal.

The copied render-pass payload now retains each reference layout. Legal GENERAL
and depth read-only references reach the original native driver unchanged. The
legacy unexposed feedback-layout enum uses Mesa's intended core GENERAL fallback
consistently for barriers, descriptors and attachment layouts; the EXT is not
advertised. No CPU rasterizer, extra pixel copy or fake extension feature is added.
Existing old guest payloads keep their original default attachment layouts.

Vulkan 31 and strict Clippy pass locally. The original GPU mousepad reproducer
now completes two unsaved-close / modal-present / cancel cycles in 21.4 seconds,
with GPU frame kind 2 and no SHM presentation. This is functional acceptance,
not a benchmark or physical iPad result. The new Apple gate checks the same modal
lifecycle and nonblank exported Metal texture pixels. That gate is pending;
it must establish whether the owner's additional native pipeline initialization
failure is also resolved, rather than assuming all Mesa warnings have one cause.
