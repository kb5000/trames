# CPU aliases in the single-level page TLB

[Production source 7c96434](https://github.com/kb5000/trames/commit/7c964341555653fb6e1b19caa551c37c859e0af5) replaces the resident three-scalar hint from the first hardware
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

- [Complete 376-test workspace](hardware-mmu/tlb-workspace.log) on final source;
  60 memory library + 4 translation cases pass natively and under ARM64 QEMU.
- All 42 CPU library + 2 register-state + 1 tail-stack cases pass on both hosts.
- Strict workspace/all-target/all-feature Clippy; 89 fully instrumented standard
  library TSan cases. TSan does not instrument raw inline assembly bytes.
- Actual guest mmap/EOF/SIGBUS/COW/code aliases, musl fault-handler contexts,
  1/2/4-CPU regions, 507 independent warmed integer snapshots and all 19 native
  pthread entries with nested callbacks, shared stacks and disk mode pass.
- Final Release FFI library passes [all five cold boot modes](hardware-mmu/tlb-reopen.log),
  including GPU re-enable and bundle update with retained data.
- iOS ARM64 Release core emits assembly. The selected scalar worker handler's
  stack frame is 112 bytes, versus 256 for the resident-hint candidate. It has no
  UDIV or hint API call. This is compile-only evidence, not physical iPad timing.

## Complete comparison and decision

The unchanged harness alternates three pairs against pre-MMU source
`538232a9243b605e11afae7fe8d87f23c4bf1116`, using the same original guest ELFs,
libc, store, pinned nightly/Release/thin LTO settings, affinity and fixed work as
all earlier batches. No builds or regression suites overlap timing. This is a
shared x86_64 development host, with uncontrolled frequency/load, not an iPad.
CoreMark uses 10,000 iterations with valid CRCs and >=10s samples; the memory loop
uses 10 million iterations; SMP uses 30 million iterations per worker.

[Source/binary provenance](hardware-mmu/tlb-provenance.json),
[all host samples](hardware-mmu/tlb-host.json),
[all SMP samples](hardware-mmu/tlb-smp.json), and
[ARM64 selected handlers](hardware-mmu/arm64-tlb.s) are retained.
Ratios are baseline/candidate elapsed time: below one means slower.

| Workload | Baseline median (s) | Page TLB median (s) | Baseline/candidate |
| --- | ---: | ---: | ---: |
| coremark | 27.233 | 27.863 | 0.977× |
| cpu-memory | 2.041 | 1.782 | 1.145× |
| vim-default | 2.014 | 2.197 | 0.917× |
| python-imports | 8.717 | 9.107 | 0.957× |
| gcc-build-run | 2.840 | 3.011 | 0.943× |
| smp-work | 2.585 | 3.026 | 0.854× |

The memory loop improves by about 14.5%; **general application acceleration is
not demonstrated**. CoreMark is slightly slower, and app/SMP medians regress.
SMP samples are variable: candidate 3.628/2.724/3.026s versus baseline
2.585/3.344/2.481s. All unfavorable observations remain. These batches do not
isolate host load, layout, register pressure or individual patch contributions;
do not compare medians from different batches as if paired.

The page-TLB implementation is retained because it removes the explicit hint API,
extra tail arguments, and repeated window/group lookup from eligible operands,
while preserving the hardware memory architecture with fewer lifetime rules and
less code. Its adoption is not a claim of universal speedup. The earlier resident
hint implementation is removed from production, with measurements/source retained
in [the historical report](hardware-mmu.md). No extra backend or benchmark-specific
runtime branch is added.

Remaining work: physical iPad ARM64 timing/profile, reduce repeated page-TLB
validation on unsupported-mapping fallback, measure the larger 24-byte entry's
cache cost, and isolate code layout/dispatch/region-guard costs. Hardware permission
checks alone do not close the native/interpreter execution gap.

## Native Apple validation and CI

[Native Apple ARM64 memory log](hardware-mmu/tlb-apple-memory.log) passes all
60 library + 4 translation cases on this exact source, including actual 16KiB
Mach mappings, protected operands and native COW neighbor invalidation.
The [Apple pipeline](https://github.com/kb5000/trames/actions/runs/38934931830)
and [Linux pipeline](https://github.com/kb5000/trames/actions/runs/38934931832)
continue their full integration/app workflows at this report update. Their eventual
full status is not inferred from local or early-job success. The earlier d516f84
Apple build passed all jobs; it does not validate later TLB changes.
