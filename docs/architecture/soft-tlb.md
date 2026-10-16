# Permission-cached guest memory and the host MMU

The default interpreter is the pinned nightly tail-call implementation. CPU and
native accesses continue to share the same guest memory contract. This change
optimizes translation and permission validation, not the allowed guest results.

## Implemented software TLB

Both coordinator Memory and worker View already had 256 direct-mapped entries
containing a virtual page and slab index. A hit avoided BTreeMap lookup but still
loaded the page's permission fields and checked them separately. The new entry
now includes a protected CPU page address: **24 bytes**, or **6 KiB** per view. Its tag packs the
virtual page, read/write/execute bits, a valid bit and a coordinator backing bit
into otherwise unused low page-offset bits. One masked comparison proves both
the page identity and requested access. Invalid entries grant no permissions,
including for page zero or addresses above the 48-bit guest limit.

For a single-page coordinator hit, a valid mapping also proves the address range;
there is no repeated checked-add or address-limit branch. Misses take the checked
cold path and preserve precise fault addresses. Permission failure is never
cached as success. Worker hits also reuse the snapshot's already validated
backing status. Cross-page accesses validate the complete range before effects.
Top-byte data tags and untagged instruction fetch semantics are unchanged.

The warmed slab index uses get_unchecked and unwrap_unchecked: only the private
page table can fill an entry, all mapping/protection edits flush the cache before
another access, and no callback occurs between translation and dereference.
Debug assertions retain these invariants in checked builds. The coordinator is
local and non-Send/non-Sync; workers use their own Cell cache over a fixed Arc
snapshot. Mapping changes require joined workers and produce fresh empty views.

The optional CPU alias is bounded to its cached guest page; no extra Rc is held.
Vec growth cannot invalidate an index or alias. Fork retires aliases only after
flushing the cache. COW promotion, discard and mapping changes invalidate cached
alias/eligibility facts; special file mappings cache no CPU byte pointer.
The ordinary Rc::make_mut, version, dirty publication and executable-write hooks
remain active. File-backed coordinator pages still check live EOF on **every**
access, including after a private COW copy. Backing methods retain their fallible
transport. A worker's EOF/permission facts are immutable for its checkpoint;
backing changes refresh the snapshot before workers resume. Native admission is
still checked separately when it decides whether execution needs a checkpoint.

The hot path removes redundant checks established by a cached mapping. It does
not erase guest permissions, file errors, code invalidation, LL/SC reservations,
region guards or atomic/fence ordering. Pure native helpers reuse these same
translations; no algorithm lock or global serialization is added.

## Hardware MMU integration

[Dedicated hardware-protected mappings](hardware-mmu.md) now implement this
path for ordinary RAM, including shared native/CPU physical storage, the single-level
direct-mapped page TLB and bounded fault recovery. Mixed iOS subpages and
special backings retain the software TLB. The constraints below explain those
boundaries; this is still ordinary userspace mmap/mprotect, without privileged
guest page tables or a JIT.

### Design constraints

The host MMU already translates the host allocations containing guest bytes.
To make it additionally enforce guest permissions, the runtime needs
**dedicated host mappings**, set their protections with mmap/mprotect equivalents,
and translate expected host protection faults back to guest exceptions. Ordinary
heap pages and worker byte allocations cannot simply be mprotected:
a host page may contain other allocations and unrelated runtime state.

Dedicated mappings are therefore a separate storage path, with these boundaries:

| Constraint | Consequence |
| --- | --- |
| Guest pages are 4 KiB; iOS ARM64 host pages are normally 16 KiB | Four adjacent guest pages can have different permissions. One contiguous host protection cannot represent that. Runtime page-size detection and subpage software checks or separately padded host slots are needed. Separate slots can multiply physical/virtual memory costs. |
| Sparse 48-bit guest addresses, high stacks and top-byte data tags | Guest addresses are not host pointers. A contiguous reserved arena needs a constrained guest window and host address-space availability, or an extra translation table. Permission hardware does not remove this address conversion by itself. |
| Fork COW, shared-file aliases and remapping | Host mappings must represent guest alias identities and independent protections while preserving private copies and shared updates. Current heap ownership must be redesigned. |
| File truncation/EOF and guest execution under an interpreter | Host page protection alone does not reproduce every backing error. Interpreted instruction bytes are host data reads, so host NX cannot distinguish guest execute permission from read permission. |
| Executable guest writes, dirty commit and exclusive reservations | These metadata effects remain even when hardware validates access. Write-protecting and trapping only the first dirty write is a different storage design and requires workload measurement. |
| Fault recovery inside Rust, native helpers and multiple workers | A process-wide signal handler must distinguish known guest accesses from real host bugs. Longjmp across Rust frames/guards cannot be used as a general shortcut. Recovery would need bounded audited assembly access sites and platform-specific signal/exception context handling, with exact guest PC and partial-effect rules. |

The hardware path represents sparse windows instead of reserving the complete
48-bit guest VA range. Compatible host-page groups use native guarded operands;
failed admission retains this compact TLB. Neither translation mechanism removes
COW, EOF, code epochs, reservation metadata or the existing concurrency protocol.
See [hardware mapping validation](../validation/hardware-mmu.md) for code generation,
measurements and remaining device checks.

[Measurements and regression validation](../validation/tail-tlb.md).
