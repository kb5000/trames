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
| eventfd, timerfd and inotify | Complete shared OFD and original atomic counter, timer state or watch instance. Worker dup, replacement and close use live membership. Accepted reads and control calls retain the original object through numeric FD reuse; prepared maps cannot keep registrations alive. Creation and pathname watch registration still join. |
| Owner controls | Directories, namespace-bound ancestry, sockets/PTY, signalfd/epoll/ring/BPF controls and sealed memfd retain explicit owner boundaries. A structural operation involving one of these controls returns before effects and joins. |
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
An in-cohort first mmap permanently transfers that PlainFile's byte authority to
MappedData. The transition takes the existing byte write lock; old direct read
and write capabilities check the transition under their existing byte lock and
defer before offset, timestamp, notification or byte effects. Keeping an accepted
OFD alive must not let its prepared cache bypass newer mapped bytes.

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

The accepted byte path includes named FIFOs as well as anonymous pipes. A FIFO
retains its pathname descriptor kind; deciding acceptance from that kind alone
would lose its original endpoint when a blocked read's numeric slot is replaced.
Anonymous shared controls use the same byte ABI, notification and timer-deadline
algorithms as owner execution. Control type validation, execution and completion
accounting use one accepted capability, including when a readiness callback
replaces the numeric slot. No new resource lock or timer thread is needed.
Joined execution also checks the accepted wait's deadline independently of its
notification bit and numeric FD table. A timer can expire without an external
notification, even after its slot is closed and reused. Idle detection and host
sleep use that retained deadline; otherwise a one-CPU read can stall or report a
false deadlock. No extra clock read is added to a byte wait without a deadline.

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

## VM-local layout checkpoints

Unfiltered, untraced brk, mmap, munmap, mremap, mprotect, mincore and madvise
requests return their original CPU/job to the execution queue. The queue pauses
the actual Memory root and the transitive set of VMs sharing a backing commit
domain. Domain identities are cold mapping metadata, not per-load checks. Every
CPU and in-flight service in that group must return before its Views are dropped
and committed. Unrelated workers and services keep running. The original kernel
mapping operation then runs once; only that group's Views are prepared again.

The queue mutex publishes job handoff. A relaxed pause bit asks a running CPU to
return at its existing execution boundary; it does not publish guest bytes.
Retained Views from older generations prevent local mutation. Unsupported new
backings, filtered/traced calls and owner-control changes retain the joined
fallback. Completed local calls are never replayed during fallback or cancel.
If a new mapping links two already-paused groups, their checkpoints merge before
either group resumes. Existing permissions, backing/pin ownership, COW, exclusive
access invalidation and native execution boundaries remain authoritative.

## Persistent execution contexts

Production cohorts retain the same allocated jobs, CPUs, translations and
accepted pending operations until an actual owner boundary, stop, error or
cancellation. The existing short control interval checks host commands, signals,
process deadlines and owner work; elapsed time alone no longer tears down the
cohort. Existing workers sleep on resource notifications, host readiness and
deadlines, releasing execution slots when blocked. An all-parked terminal cohort
can accept input and resume in the same context. No new thread or polling message
bus is used.

GuestOutput's live poll hook is restricted to host queues and safe metadata;
legacy adapters default to requesting their joined control hook. The iOS adapter
checks cancellation and pending commands, pumps established PTY byte channels
under their existing locks and takes scalar capacity snapshots. Terminal
creation/removal, resize signal delivery, plain owner terminal streams, native
SCM_RIGHTS/namespace service graphs and other process/FD topology changes still
join. One active CPU retains Memory's direct MMU path.

Metrics separate local checkpoint groups/spaces/syscalls and persistent control
checks from global cohorts, preparation, resource processing and stop costs.
Actual-worker regressions hold a mapping destructor while observing unrelated
guest progress and related guest quiescence. Other tests retain original I/O
through FD reuse, checkpoint merging, service completion, cancellation and budget
exhaustion. This completes the admitted VM/context paths, not every kernel
resource ownership migration.

Three alternating samples of the unchanged cohort-cost ELF on the native ARM64
Linux CI VM reduce the four-thread mmap/munmap median from 0.223 s to 0.159 s,
with global cohorts falling from 3,221 to 12. The candidate executes a median
4,100 local mapping calls; local commit groups can batch multiple requests.
The four-thread compute case falls from 319 global cohorts to 13, with a wall
ratio of 0.930. Separate one-CPU file/mapped-file samples retain zero cohorts
and host CPU median ratios of 1.015/0.976. These short synthetic samples have no
confidence interval and do not establish application or iPad throughput. Exact
binaries, inputs and overlapping cost windows are recorded in the
[VM/context follow-up evidence](../validation/shared-fd-worker-close-summary.json).

## Remaining release gates

Complete source-bound local CI before pushing, then final hosted checks and
physical iPad smoke. Verify cached Docker startup, terminal responsiveness,
Files preview/edit/deferred replay/conflict recovery, large transfer and
foreground/reopen behavior. Excluded FD/control ownership remains explicit;
Firefox remains optional.

Preserve Memory's direct MMU path for one active CPU. Do not infer performance
from thread count, zero service_requests or opcode coverage: measure preparation,
resource processing and stop/commit costs separately with identical inputs.
Historical measurements on another host/source do not establish Apple product
throughput. Passing unchanged checks are reused; changed, failed and unrun cases
are verified at the appropriate scope.
