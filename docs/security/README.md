# Runtime security and compatibility limits (M0–M6)

ELF files, instructions, addresses and argv/envp are untrusted. Parsers validate
lengths, offset arithmetic, architecture, flags, alignment, entry permissions and
segment ranges. Dynamic ET_DYN/PT_INTERP are supported; overlapping PT_LOAD
pages remain rejected. Interpreter files come only from the checked guest VFS. Guest faults are errors, not host pointer dereferences.

| Resource | Current bound |
| --- | --- |
| Input ELF | 256 MiB (the CLI reads at most the limit plus one byte) |
| Committed guest page objects including stack | 512 MiB per address space; 1 GiB scheduler aggregate |
| Virtual tasks including threads and zombies | 256 |
| Pipe buffer / atomic write | 64 KiB / 4096 bytes |
| PTY input, output and echo | 64 KiB each; empty EOF records also consume input budget |
| Nested signal frames | 8 |
| Program headers | 1024 per ELF |
| Symlink traversal | 40; targets at most 4095 bytes |
| Rootfs compressed / expanded input | 16 MiB / 128 MiB, pinned SHA-256 |
| Rootfs entries / individual file | 16,384 / 64 MiB |
| Initial stack | 1 MiB mapping, non-executable, unmapped page below it; argv/envp use at most 64 KiB |
| Private anonymous PROT_NONE | Sparse virtual ranges within the 48-bit guest space; at most 65,536 reservation intervals. Accessible pages still count against the memory budget |
| argv/envp entries combined | 4096 |
| Virtual file data (all live files) | Disk: 16 GiB initial logical budget, configurable and volume-bounded; memory: 16 MiB |
| VFS live nodes / namespace entries | Disk: 131,072 by default, configurable up to 262,144; memory: 1024 |
| Serialized manifest input | 128 MiB, checksummed and structurally validated |
| Import/export buffer | 64 KiB |
| File descriptors | 256 per process |
| Guest Unix sockets | 1024 live endpoints, including queued accepts; 64 KiB / 128 queued messages per direction |
| Inode xattrs | user.* only; 128 attributes and 64 KiB per inode, 1 MiB per filesystem |
| Native sockets | 64 per session, shared across fork/dup, including accepted sockets |
| Socket buffer requests | 64 KiB initial send/receive each; guest requests at most 256 KiB each; actual OS sizes may differ |
| UDP scratch buffer / TCP transfer | 64 KiB / at most 1 MiB |
| Listen backlog | at most 128 |
| Virtual path / component | 4095 / 255 bytes |
| Guest address domain | below 2^48 |
| Single output syscall | at most 1 MiB copied; larger requests may return short writes |
| CLI default instruction budget | 10,000,000 |

These restrictions are deliberate limits and can reject valid Linux programs.
Memory is not mapped to host executable pages. Code page permissions follow the
ELF; writable/executable guest segments are allowed, but remain interpreted data.

write validates the descriptor and guest address before output, including the
address domain for zero-length calls. Read access is required for non-empty
buffers. Output sinks are called once; interrupted or partial writes are returned
to the guest. CapturedOutput is a testing convenience with an unbounded Vec;
applications processing untrusted guests must provide a bounded output sink or
stream output as the CLI does. An instruction budget does not impose a wall-clock
or output quota on the embedding application's sink.

The runtime is not a hardware isolation boundary. Safe Rust and checked guest
memory reduce a class of bugs; parser and host integration defects can still
affect the containing app. No audit, broad parser fuzz campaign, sanitizers,
iPad device test, native SSH or host resource bridge has been completed in M0.
SSH host key verification and consent for sandbox resources remain required
when those subsystems are implemented.

Guest path operations access the private virtual namespace. The disk backend uses
fixed inode-object names beneath an app-owned storage directory, never guest
path components as host names. Explicit imports authorize reading selected host
streams; exports authorize new host destinations. Neither grants a general host
path adapter. Copies do not automatically write back to their sources. There are
guest symbolic links cannot leave this namespace; there are no general host
directory mounts.
New file reads validate destination buffers before consuming positions. Vectored
output is preflighted and invokes the sink once; partial side effects are never
retried automatically. Anonymous fixed mappings are checked against resource
limits before replacing data. Supported native entropy supplies AT_RANDOM and checked guest getrandom.

M2 virtual pipe/terminal waits suspend processes. Input adapters must implement
nonblocking poll_stdin/stdin_ready; terminal master events scope signals to guest
jobs. No host PID, host exec or guest-named host device is forwarded. Nonblocking
plain console I/O and alternate signal stacks remain limited/unsupported.
The 0.2.0 follow-up implements selected cooperative shared-VM threads, vfork,
futex queues and POSIX timers; it creates no guest-directed host thread. Shared
memory is charged once per address space; task count remains bounded at 32.
Volatile proc/sys/dev views expose guest state and never host processes/FDs.
Virtual ownership and guest root change no host identity or app permission.
Session-root exit drops remaining virtual children; no daemon is retained.
Disk persistence and 16 GiB initial file budgeting are implemented. File bytes,
including sparse logical ranges and open-unlinked files, consume the budget once
per inode. Large files do not become equivalent RAM allocations. Guest mappings
remain 64 MiB per process and 256 MiB aggregate.

Storage is an app-private directory, protected by an advisory OS file lock while
any process clone retains it. Known internal entries must have the expected type;
symlinked objects and locks are rejected. This does not defend against a hostile
host process concurrently replacing directory components or bypassing the lock.
Use an app container, not a directly writable cloud provider folder. On Unix, new
store directories and internal files use 0700/0600 modes.

The bounded manifest is versioned, checksummed and validated before orphan
cleanup. The checksum detects accidental corruption and is not authentication.
Namespace replacement is atomic; fsync/fdatasync/sync and normal CLI teardown
request data and directory durability. SIGKILL recovery is tested; power-loss,
provider, filesystem and iPad-device durability have not been established.
VolumeSpace is a platform-supplied snapshot rather than a host-space reservation;
changed host conditions can still cause an IO error. Individual import parent
directories can remain after a later host failure, as each namespace operation
commits separately.

Native iPad Files access still requires the frontend to authorize, coordinate,
cancel and release resources. Core stream APIs grant no security-scoped permission
by themselves. No hardware isolation, full Linux security semantics or audited
sandbox is claimed.

The library starts with networking off; the CLI defaults to outbound networking
and loopback listeners. Non-loopback listeners require --network full. Socket
operations translate structures, flags and errno, preflight all guest buffers,
and keep native descriptors nonblocking. Loopback accesses native loopback, and
wildcard ephemeral UDP sockets can receive inbound traffic. These native-backend modes are coarse
capabilities; the optional virtual backend is described below. Host policy and
OS permissions still apply. The instruction budget is not a network deadline.

Host SIGPIPE is suppressed; guest SIGPIPE follows the virtual signal model.
Session exit and CLI faults release all socket owners. Socket objects are not
persisted. Unsupported host raw/Unix sockets, ancillary messages, socket timeouts and
other options fail explicitly. Guest TLS/certificates/apk signatures are validated in M3 and through the virtual
gateway; iPad network permissions still need device validation. See
[network architecture](../architecture/network.md) and the
[dependency/license record](dependencies.md).

M3 dynamic linking executes the guest musl loader and guest OpenSSL/apk unchanged.
Private mmap copies bounded file snapshots and validates before fixed replacement;
shared mappings, page-cache coherence and SIGBUS beyond EOF are not implemented.
Archive paths and content are validated before initialization writes; IO failure
can leave a partial initial store, which must be replaced for retry. The pinned
rootfs owner maps to virtual user 1000 and privilege bits are stripped. Guest
HTTPS uses explicit CA/proxy configuration; Alpine signatures remain required.
See [dynamic userspace](../architecture/dynamic-userspace.md) and
[M3 validation](../validation/m3.md).

The delivered optional virtual backend uses an isolated userspace packet fabric:
private IPs, loopback and ports, no host virtual interface or native raw socket.
Only a trusted gateway opens ordinary nonblocking native TCP/UDP sockets. It
preserves guest TLS and host policy. Trusted host mappings require an explicit
embedder/CLI choice and cannot be requested by a guest. Private subnet addresses
never fall through to native hosts. New bounds per fabric are 16 environments,
256 stack handles/reservations, 64 native flows and 96 frames per port/direction;
reassembly uses at most two bounded datagrams per node, with overlap poisoning
and expiry. DNS uses two bounded workers and request tokens; namespace generations
prevent reused IDs from receiving late work. See the precise buffer/time limits
in [virtual-network architecture](../architecture/virtual-network.md) and the
[executable validation](../validation/virtual-network.md).

The 0.2.0 guest raw ICMP API is handled by the userspace stack. External echo
uses unprivileged native ICMP datagram sockets, with actual OS errors and bounded
flows; no host raw socket, route mutation or VPN entitlement is added. Route
netlink queries are synthesized from the guest namespace, never host netlink.
See [Linux compatibility](../architecture/linux-compat.md) for exact scope.

## M5 native boundary

Bridge native resource methods require per-request consent; frames, queues,
connections and deadlines are bounded. Guest UID 0 grants no native authority.
Provider copies stay inside explicit directory bookmarks, reject symlinks and
traversal, obey streaming limits and revocation, and create destinations
exclusively. See [M5](../architecture/m5.md).
Software sanitizers and integration checks do not constitute a security audit.

## CPU compatibility profile

The single Rust interpreter retains checked guest memory and bounded dispatch.
The isolated native compute crate accepts values and uses static instructions;
it never receives guest pointers or creates executable memory.

By the owner's compatibility policy, PAC authentication and BTI landing checks
are disabled, and MTE allocation tags read as zero. Branch, link, authenticated
load, address arithmetic, writeback and zeroing effects still execute with
ordinary virtual mapping and data-permission checks. PAC/BTI/MTE are not advertised
as full Linux hardware capabilities. This profile provides no authentication or
tag-based memory-error protection. See [the exact scope](../validation/instruction-coverage.md).
