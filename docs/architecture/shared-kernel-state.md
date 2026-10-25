# Shared kernel resources and execution domains

Updated 2026-10-26. This describes the current ownership boundaries and the
remaining release gates tracked in [issue #36](https://github.com/kb5000/trames/issues/36).
The existing worker pool executes native calls and short syscalls directly.
Blocking calls return their execution slot and resume through existing resource
notifications, host events or deadlines. No resource-service thread, polling bus
or VFS-wide lock is introduced.

## Files membership and actual OFDs

`SharedFiles` is the authoritative membership object for one Linux files_struct.
Its object-local mutex protects lookup, allocation and replacement; a relaxed
revision invalidates cold preparation caches. Lookup returns a strong reference
to the actual admitted OFD. No table guard survives into resource I/O or final
close callbacks. CLONE_FILES shares this exact table; fork copies membership and
CLOEXEC while preserving the underlying OFDs.

| Resource | Current ownership and execution boundary |
| --- | --- |
| Ordinary nonvolatile, unsealed pathname files | Complete shared OFD, inode and eligible mount ancestry. Live worker dup/F_DUPFD, dup3 and close. File bytes use prepared authority where eligible; pathname/storage changes still join. |
| Pipes and eligible named FIFO descriptions | Complete shared OFD and actual endpoint lifetime. Final release updates EOF/HUP/EPIPE and wakes peers on the releasing worker. FIFO poll uses pipe state even though its descriptor kind contains a pathname. |
| Owner controls | Directories, namespace-bound ancestry, sockets/PTY, anonymous event/timer/inotify/epoll/ring controls and sealed memfd retain explicit owner boundaries. A structural operation involving one of these controls returns before effects and joins. |
| Descriptor metadata | OFD status and per-FD CLOEXEC are live atomic state. Fork copies descriptor bits; duplicates share status. Ordinary metadata counters/bits use relaxed ordering. |

Shared close releases the calling process's POSIX record locks on any relevant
close, including when another alias, accepted request or mapping keeps the OFD
alive. Flock and close notifications follow the final actual OFD reference.
Replacement installs the new entry before releasing the old one. Teardown,
notifications and lock release occur outside the membership mutex. Filtered
structural syscalls retain their joined path so a filter is evaluated once.

This is an admitted-resource migration. The owner-control row remains a real
boundary; shared leaf data alone does not migrate its complete control object.

## Preparation cannot define resource lifetime

DescriptorVersion caches reusable prepared owner capabilities, without binding
them to a parent or child files_struct. Each DirectFds binds the slice to its
caller's actual membership. FileEpoch keys table captures by the membership
allocation/revision and execution policy. The capture keeps that allocation
alive, preventing identity reuse; an Rc version address is not a table key.

Ordinary-file admission prepares one weak policy record per actual OFD and
policy, rather than per numeric alias. It records stat/policy fields, weak
notification routes and weak data/position/lock identities. Each accepted
DirectFile promotes the required references and owns the complete original
SharedFd. Byte operations do not repeat pathname/stat capture or borrow the
inode for authority checks. Append, growth, finite limits, seals, mapped aliases
and synchronous storage keep the existing deferral and validation rules.
FileEpoch's metadata reconciliation registry is weak too: it cannot retain an
unlinked PlainFile allocation until the next join.

A raw capability cache is not an accepted operation. Prepared epoll watches use
weak original-OFD identity for shared targets, so queued stale readiness cannot
keep a pipe open or retarget a reused FD. Weak allocation tokens prevent ABA
without retaining the payload. Tokens are captured after construction, preserving
constructor use of Arc::get_mut.

Accepted byte calls and flock requests retain their original complete OFD
through numeric close/reuse and owner/worker handoff. Dispatch moves a parked
shared request out of Process. Keeping an extra Process reference until the
next join would delay actual final close and is prohibited. Stop/continue
preserves a wait; completion, cancellation, caught signals, exec and exit release
it. Original-endpoint SIGPIPE still follows the existing signal path.

## Poll, io_uring and mappings

ppoll arrays and pselect bitsets are imported once. Their accepted shared OFDs,
timeouts and masks survive changed or unmapped guest argument buffers. Owner
controls rebind only to the same weak OFD allocation identity; a reused FD or an
independent VFS with a coincident local counter cannot retarget the wait.
pselect retains its joined execution boundary. Named FIFO readiness uses its
real pipe queue, writer epoch and subscriptions, rather than regular-file
readiness. This distinction is required by the signed crun exec FIFO protocol.

Normal shared io_uring SQEs resolve live membership at acceptance. The request
owns the original byte/file capability; completed requests do not leave extra
shared originals/descriptors in an owner recovery map. A parked request restores
its joined view from that same retained object. Fixed registrations retain their
legitimate registration lifetime. Requests use the original submitting VM's
buffer authority, never an observer's same-numbered virtual address. Final
request destruction runs outside the ring mutex, including cancellation and
linked-timeout batches. Ordinary completion returns one retired value without
allocating a vector.

File-backed mappings retain their original OFD through read-only/private/shared
mappings, COW, fork, discard, protect, partial replacement and remap. Writable
mapping leases retain the original shared open description and mount identity;
owner-only mappings keep an explicit owner lease. io_uring mappings likewise
retain the ring and registered files until their final mapped reference ends.
Inode lifetime, storage allocation and quota tests check actual resources,
not stand-in counters.

Retained mount/filesystem data is separate from owner parent topology. Busy
unmount, detached trees, namespace ancestry and overlay layers preserve their
actual resources without keeping an obsolete parent cycle alive. Owner-only
namespace ancestry is excluded from shared pathname admission.

## Memory and synchronization rules

CPU interpretation and native acceleration use the same guest access,
permissions and concurrency model. Stable mapped bytes retain their backing;
layout/generation invalidation occurs at the existing boundaries. Do not add a
per-load FilePage layout lock, a second permission policy or ordinary memory
barriers. The rejected layout-lock prototype regressed serial mmap CPU time by
about 26–27%; see [cohort measurements](../validation/cohort-cost.md).

Existing resource algorithms keep their own synchronization responsibility.
Atomic operations and synchronization primitives preserve the required ordering;
ordinary AArch64 accesses and metadata do not simulate stronger consistency.
There is no blanket unsafe Send of VFS/Memory/Rc graphs. Controlled unsafe must
identify its compiler/architecture and lifetime conditions and bound effects on
guest state, host memory and other threads.

Details: [region memory](region-memory.md), [native allocator](native-allocator.md),
[shared inode evidence](../validation/shared-inode-capabilities-summary.json),
[retained byte evidence](../validation/shared-fd-retained-byte-summary.json),
[current local CI evidence](../validation/shared-fd-worker-close-summary.json), and
[overlay lifetime evidence](../validation/shared-overlay-lifetime-summary.json).

## Remaining execution-domain gates

The current cohort still freezes execution views and joins globally for owner
control and mapping commits. Worker-local close for admitted resources does not
complete VM/task ownership or all kernel execution migration.

1. **VM-local commits:** synchronize only the affected VM and genuinely related
   backing domains. Preserve permissions, translations, pin/COW/exclusive/native
   lifetimes and prove that unrelated VMs keep progressing.
2. **Persistent contexts:** retain runnable/parked jobs and translations across
   unrelated events. Use existing workers and event sources; blocked work
   releases a slot. A longer quantum is not a substitute for event handling.
3. **Release acceptance:** complete source-bound local CI before pushing, then
   final hosted checks and physical iPad smoke. Verify cached Docker startup,
   terminal responsiveness, Files preview/edit/deferred replay/conflict recovery,
   large transfer and foreground/reopen behavior. Firefox remains optional.

Preserve Memory's direct MMU path for one active CPU. Do not infer performance
from thread count, zero service_requests or opcode coverage: measure preparation,
resource processing and stop/commit costs separately with identical inputs.
Historical measurements on another host/source do not establish Apple product
throughput. Passing unchanged checks are reused; changed, failed and unrun cases
are verified at the appropriate scope.
