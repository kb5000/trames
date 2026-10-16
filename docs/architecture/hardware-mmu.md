# Hardware-protected guest RAM

Ordinary anonymous RAM has a hardware-protected CPU alias and an RW kernel/native
alias of the same physical bytes. Scalar interpreter operands execute native
loads/stores through the CPU alias. `mprotect` enforces guest read/write rights;
there is no repeated software permission grant on that path. No host mapping is
executable, and no code is generated at runtime.

## Sparse addresses and one-level cache

Guest pointers remain 48-bit addresses with the established top-byte handling.
A single contiguous reservation of that whole address space is impractical on
an iPad. Instead, each touched 16 MiB guest window gets a separate host mapping.
Low heap and high stack addresses do not require allocating their intervening VA.
Linux uses a RAM-backed memfd and closes its descriptor after mapping both aliases.
Darwin uses anonymous memory and same-task `mach_vm_remap(copy=false)`; it requires
neither another process's task port nor JIT/executable-memory entitlements.

Each coordinator or worker view owns a **256-entry, single-level direct-mapped
page TLB**. Each 24-byte entry caches the guest page/permission tag, a stable frame
ID, and the protected CPU page address (zero for unsupported mappings). The cache
uses 6 KiB per view. An operand hit needs one guest-page tag match and the page
offset; hardware checks its read/write permission. Eligibility and sparse-window
lookup happen at fill time, rather than once per memory instruction. Special
mappings retain the same packed permission tag and checked transport.

A separate 16-entry direct-mapped descriptor cache assists cold window lookup
and mapping/COW preparation; it is not another page-translation level. Descriptor
collisions and cached absence only change fill work. The interpreter has its
original three tail-call arguments. It no longer carries an owner/descriptor/base
hint through ordinary ALU instructions. The earlier resident-base experiment and
its mixed performance are retained in the [validation report](../validation/hardware-mmu.md).

Cached CPU addresses own no storage. Mapping, protect, unmap, discard and remap
flush the page TLB; fork flushes before retiring any parent CPU aliases and sharing
byte owners. Native/CPU COW promotion flushes software-only entries when eligible
storage changes. Frame slab growth cannot invalidate an alias address. Worker
views retain the complete immutable snapshot and have private empty caches when
created/refreshed; an older retained view keeps its older physical mappings.

## Hardware granularity and fallback

Guest pages are 4 KiB. A host group may be 4, 16 or 64 KiB. A group is eligible
only when every subpage exists, has uniform permissions, and has ordinary private
RAM storage. Holes, mixed permissions, live file/device backings and write-only
mappings use software translation. Host write permission can imply read permission,
so write-only guest mappings are deliberately excluded. Execute permission and
instruction fetch still use the interpreter's checked code path. CPU writes to
executable guest memory use the existing code-invalidation path.

Eligible 1/2/4/8/16-byte operands must remain within one guest page. Inline asm
contains the exact memory instruction and its local recovery label. A link-retained
fixup table associates only those instruction PCs with their recovery sites.
SIGSEGV/SIGBUS recovery additionally requires a hardware-generated fault and a
fault address inside a live registered CPU window. The handler changes the saved
PC and returns normally through the kernel signal frame. It does not longjmp,
unwind Rust, allocate, acquire locks, or skip a byte-region guard/destructor.
Failed operations return to the checked path for the precise guest fault.
Cross-page operations preflight every page before any store effect.

Unrelated host faults retain default termination or the previously installed
handler. Its signal mask, NODEFER, RESTART and queryable one-shot disposition are preserved.
The MMU retains its handler after forwarding a returning callback. As with other
process signal integrations, replacing SIGSEGV/SIGBUS handlers after MMU startup
must chain this handler too. A registered range alone never authorizes recovery
from an unrelated host instruction or guest-controlled PC.

Darwin/XNU does not include `ps_sigreset`/`SA_RESETHAND` in its public
`sigaction(..., old_action)` result, even though setting the flag works. A native
query-and-restore also loses it. Automatic chaining cannot reconstruct a
pre-existing hidden one-shot flag: such integrations must supply/preserve the
original registration separately. This is a documented host-integration limit;
ordinary default/Rust handlers and observable action flags are covered. The
[public XNU getter](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/kern/kern_sig.c)
and independent original/query-roundtrip subprocesses establish this boundary.
No platform implementation source is imported by this observation.

## COW, native code and parallel execution

Coordinator pages own `Rc<PageBytes>`; the bytes can be heap storage or the RW
mapped alias. Cloning byte storage copies it, preserving `Rc::make_mut` semantics.
Fork first retires CPU mappings and clears the cache, then shares the existing
page owners. It does not eagerly copy guest RAM. A subsequent write promotes its
host group into a private window before CPU/native mutation. Remap retires aliases
before moving pages; protection, unmap, discard and fixed replacement refresh
eligibility/protection at quiescent boundaries. Mapping failure falls back to the
existing software implementation.

Workers retain immutable mapping snapshots and share physical RAM slots. Their
CPU aliases map those exact slots; native byte slices address the RW alias. A
retained snapshot owns its old storage when mappings or root-frame slots change.
Incremental snapshots update only changed host groups. Physical-window leases
prevent reusing a live slot. Dead pool metadata is pruned at full rebuilds.

Ordinary worker CPU/native accesses keep the **same 64-byte region guard**;
atomics, exclusives, acquire/release fences, reservation epochs, dirty-bit commit
and executable epochs retain their established protocol. Inline asm creates no
unbounded Rust slice at a possibly inaccessible address. Raw access is bounded
by the cached guest-page match, fill-time group eligibility and lifetime ownership; concurrent access is bounded by
the same guard as native code. This changes translation/permission work, not a
function's original thread-safety contract, and adds no algorithm/global lock.
Backed mappings retain live EOF, alias, writeback and checkpoint behavior.

The registry caps live CPU aliases at 2,048 windows; exhaustion falls back to
software. Each alias reserves 16 MiB of VA, not 16 MiB of physical RAM. OS page
tables, descriptor arrays, setup syscalls and COW group copies still cost memory
and time. Different 4 KiB slots within a 16 KiB host group may become physically
fragmented after replacement; these groups safely fall back rather than widening
permissions or sharing the wrong bytes.
