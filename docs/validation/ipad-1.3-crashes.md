# iPad 1.3.0 build 35: memory and Xwayland crash fixes

2026-10-22. Implementation is on main; the published 1.3.0 build 35 IPA is
unchanged. No raw iPad report, personal identifiers, addresses or Firefox
logs/screenshots/stacks/dumps are published.

## Firefox host allocation abort

The device report records a Rust host allocation abort in
`parallel::prepare_frame`, rather than a recorded jetsam kill. A reproducible
memory amplification is fixed in [783271b](https://github.com/kb5000/trames/commit/783271b582a36791aa65d80c6597c82f5fdd124d):
after parallel publication/fork, untouched anonymous pages may have mapped/shared
byte owners without resident charges. Hardware MMU host-group relocation copied
those logical zeros into new physical RAM. Normalize still-uncharged anonymous
owners to demand-zero before relocation. Backed/resident pages, first-write
charges, fork isolation and existing permissions retain their rules. Ordinary
CPU/native loads/stores gain no checks, locks or barriers.

The new regression fails before the fix. [Linux/Apple ARM64 CI](https://github.com/kb5000/trames/actions/runs/39656197539)
passes Memory 94 on each platform, relevant strict Clippy and iOS FFI. Coverage
includes 16 KiB host groups, actual Linux `mincore` nonresidency, first real
write charging, adjacent pages and both directions of fork isolation.

A supplemental production Release Firefox launch uses four CPUs, 4 GiB
aggregate/per-process quotas, default preferences/sandbox and Wayland proxy,
with explicit `LIBGL_ALWAYS_SOFTWARE=true` only for the Linux Lavapipe reference.
A window appears at 66.55 seconds; at 180 seconds the target JS/native-GPU page
gate still fails (SHM only). No host allocation abort occurs in that interval;
final guest diagnostics are incomplete. This is **not** a Firefox GPU pass or
proof that all physical iPad memory pressure is resolved. Raw diagnostics remain
private. Physical iPad startup and the known default-refresh issue remain open
in [#33](https://github.com/kb5000/trames/issues/33).

## Original mousepad X11 save dialog

Three concrete native graphics gaps were corrected, using the original
Mesa/Zink and pinned MoltenVK/SPIRV-Cross implementations:

1. Legacy Mesa uses `ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT` in render passes even
   without exposing that extension; its barrier/descriptor path already selects
   core GENERAL. [dc88369](https://github.com/kb5000/trames/commit/dc883698193b41872672a685333291dac496940b)
   retains legal per-reference GENERAL/depth-read-only layouts and aligns the
   legacy enum with GENERAL. Old guest payloads retain their default layouts;
   the EXT is not advertised. Descriptor decoding is unchanged.
2. The cloud Apple Paravirtual device rejects transparent-black border-color
   samplers. [9991a00](https://github.com/kb5000/trames/commit/9991a0064076aa5f0239826d22c78ad7cdd662a7)
   uses original Metal ClampToZero for mutable core transparent-black borders.
   Other colors and upstream constexpr handling are unchanged. Native Metal
   nearest/bilinear/outside-texel/RGB/alpha pixel checks pass; this correction
   alone does not fix the modal crash.
3. The failing shader writes a vertex vec4 at location 0 but consumes two
   fragment vec2 variables at components 0 and 2. Vulkan links components;
   old MSL creates a `locn0_2` fragment input with no vertex counterpart.
   [b2ff6b5](https://github.com/kb5000/trames/commit/b2ff6b5a0b6223908490c69e2cb929ab597a2d68)
   reuses SPIRV-Cross's existing location packing/swizzle code for compatible
   32-bit vertex outputs/fragment inputs. Arrays, matrices, mixed scalar types
   or interpolation qualifiers retain previous handling. Transform feedback,
   including native Xfb mode, is excluded from this new path.

The pinned original compiler reproduces the unmatched interfaces in both
packed-to-split and split-to-packed directions. The patched compiler produces
one matching vector and the required xy/zw swizzles. There is no replacement
shader algorithm, CPU rasterizer, extra production pixel copy, per-draw barrier
or falsely advertised feature. GPU remains the default. Disabling texture-barrier
extensions, forced synchronization, classic Metal descriptor binding, disabling
optimized pipelines and GTK server decorations were unsuccessful diagnostics;
none becomes a product setting.

## Final targeted verification

[Final Linux/Apple Metal run](https://github.com/kb5000/trames/actions/runs/39675520457)
is successful for production sources b2ff6b5:

- Both new component shader pairs pass the existing independent three-round
  barycentric/color/alpha/scissor GPU readback oracle on Linux and Apple:
  294,912 readback pixels per platform; ambiguous triangle edges are excluded
  from the numerical oracle, which checks over 90% of each image.
- The old native runtime is required to fail first. It reaches
  `vkWaitForFences` / device lost and guest abort, rather than failing to find
  an input file. The new runtime passes the same inputs.
- Original signed mousepad, original glamor and Mesa complete two
  edit/unsaved-close/modal-present/cancel cycles with guest defaults (host tracing
  and Metal API validation enabled for this test). The
  exported actual Metal textures pass a nonblank-content check. That check
  is not an exact UI screenshot oracle. The complete component-plus-dialog
  probe takes 56.60 seconds; this is functional acceptance, not a benchmark.
- New/old native pipeline-cache UUIDs differ with the original build revision.
- The synthetic Metal evidence ZIP SHA256 is
  `210786c0f2cb01f59ffc4cd683617ded314e4ec54441bf708c978e38d1465da5`.
  ZIP CRC, result JSON, negative-reference failure and native patch hashes
  were checked after download.

Vulkan 31 and relevant strict Clippy were already passed; unchanged Rust
checks, Memory94, TF, other graphics, CPU, Linux, network and frontend suites
are reused. The earlier four GENERAL/legacy-feedback render-pass pixel modes
also passed native Metal. The complete source verifier now applies all native
patches to the SHA-verified pinned archives; the source bundle already includes
the whole patch directory, original dependency archives and build recipe.
Later records/source-verifier-only changes do not alter the tested native
shader or memory implementations and do not repeat runtime tests.

The Apple runner uses a paravirtual Metal GPU, not the owner's physical M3.
Real iPad mousepad/Firefox retesting is still needed. Optional missing Zink
features are not claimed implemented. [#35](https://github.com/kb5000/trames/issues/35)
remains open for broader X11/GLX/input and physical-device coverage.
