# CPU aliases in the single-level page TLB

This candidate replaces the resident three-scalar hint from the first hardware
MMU implementation with CPU page addresses in the existing 256-entry TLB.
It keeps sparse host mappings, hardware page protection, guarded inline operands,
native/CPU physical aliasing and the region/atomic/dirty protocol. The original
three-argument tail ABI is restored; no alternative CPU backend is added.

Each entry is 24 bytes (6 KiB total per view). Eligible hits match a guest-page
tag and use CPU-page + offset. Window lookup and host-group eligibility are
checked when filling the entry. Unsupported groups cache a zero CPU address and
retain software permission/backing checks. Native COW promotion flushes cached
eligibility; fork flushes before alias retirement. The regression warms entries
across fork, native promotion, adjacent pages, discard, remap, reuse, collisions,
protection changes, retained worker snapshots and shared wrappers.

Single-page file-backed writes skip hardware-group reconstruction: backing pages
are always ineligible, including after private COW, to retain live EOF checks.
This does not omit backing permission, transport or checkpoint validation.

Local validation before publication:

- Complete 375-test workspace before the final backing shortcut/new regression;
  final 60 memory library + 4 translation cases pass natively and under ARM64 QEMU.
- All 42 CPU library + 2 register-state + 1 tail-stack cases pass on both hosts.
- Strict workspace/all-target/all-feature Clippy; 89 fully instrumented standard
  library TSan cases. TSan does not instrument raw inline assembly bytes.
- Actual guest mmap/EOF/SIGBUS/COW/code aliases, musl fault-handler contexts,
  1/2/4-CPU regions, 507 independent warmed integer snapshots and all 19 native
  pthread entries with nested callbacks, shared stacks and disk mode pass.
- iOS ARM64 Release core emits assembly. The selected scalar worker handler's
  stack frame is 112 bytes, versus 256 for the resident-hint candidate. It has no
  UDIV or hint API call. This is compile-only evidence, not physical iPad timing.

A complete three-pair alternating comparison with the original pre-MMU baseline
is running on the shared x86_64 development host. The same unchanged harness,
store, guest programs, pinned compiler, affinity and fixed-work parameters are
used. No builds or regression suites overlap timed batches. Earlier unfavorable
resident-hint measurements remain in hardware-mmu.md; no speedup is claimed
before the new batch completes. Exact-source Linux and native Apple CI must
validate the new source separately from the already-green Apple resident build.
