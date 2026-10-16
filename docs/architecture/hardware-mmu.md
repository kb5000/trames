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

Each coordinator or worker view owns a **16-entry, single-level direct-mapped
window cache**. An entry is a guest-window tag and a non-owning descriptor pointer
(16 bytes). Indexing folds high window bits so common code/heap/mmap bases do not
all collide. Hits avoid the sparse ordered map; misses replace exactly one slot.
Collisions change lookup work, never data, permissions or mapping ownership. The
existing 256-entry direct-mapped page/permission TLB remains the fallback for
special mappings; this window cache is not a second associative page-TLB level.

The interpreter also carries an opaque descriptor, owner generation, and
`guest_mem_base` bias as explicit preserve-none tail-call arguments. A hit checks
the owner generation and window range before using `guest_mem_base + guest_va`.
The owner key changes before retired descriptors are freed. Hints are safe to
reuse after fork/remap or with another memory object: the generation rejects
them before any pointer dereference. The raw reconstruction API is unsafe and
requires unchanged parts from one real hint. Syscall, native-call and slice exits
retain no hint. Reference/complex instructions use the ordinary memory contract.
The compiler selects physical argument registers; residency and timing evidence
belongs in the [validation report](../validation/hardware-mmu.md).

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
handler. Its signal mask, NODEFER, RESTART and one-shot disposition are preserved.
The MMU retains its handler after forwarding a returning callback. As with other
process signal integrations, replacing SIGSEGV/SIGBUS handlers after MMU startup
must chain this handler too. A registered range alone never authorizes recovery
from an unrelated host instruction or guest-controlled PC.

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
by the window/group check and lifetime ownership; concurrent access is bounded by
the same guard as native code. This changes translation/permission work, not a
function's original thread-safety contract, and adds no algorithm/global lock.
Backed mappings retain live EOF, alias, writeback and checkpoint behavior.

The registry caps live CPU aliases at 2,048 windows; exhaustion falls back to
software. Each alias reserves 16 MiB of VA, not 16 MiB of physical RAM. OS page
tables, descriptor arrays, setup syscalls and COW group copies still cost memory
and time. Different 4 KiB slots within a 16 KiB host group may become physically
fragmented after replacement; these groups safely fall back rather than widening
permissions or sharing the wrong bytes.
