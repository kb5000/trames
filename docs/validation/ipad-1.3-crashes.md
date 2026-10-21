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
quota, adjacent pages and both directions of fork isolation. Apple 16KiB validation
is pending; a physical iPad Firefox recheck is still required before claiming the
reported end-to-end crash is completely resolved.

The separate mousepad X11 unsaved-close dialog fails native pipeline creation on the
owner's Apple GPU and subsequently aborts Xwayland. Missing optional feature warnings
alone do not establish its cause. A targeted original mousepad dialog reproduction
and native driver diagnostics are being prepared. Existing passed unrelated graphics,
CPU, Linux, network and frontend checks are reused; the released IPA is unchanged.
