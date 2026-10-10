# Runtime security and compatibility limits (M0–M3 and storage)

ELF files, instructions, addresses and argv/envp are untrusted. Parsers validate
lengths, offset arithmetic, architecture, flags, alignment, entry permissions and
segment ranges. Dynamic ET_DYN/PT_INTERP are supported; overlapping PT_LOAD
pages remain rejected. Interpreter files come only from the checked guest VFS. Guest faults are errors, not host pointer dereferences.

| Resource | Current bound |
| --- | --- |
| Input ELF | 64 MiB (the CLI reads at most the limit plus one byte) |
| Mapped guest memory including stack | 64 MiB per process; 256 MiB aggregate logical mappings |
| Virtual processes including zombies | 32 |
| Pipe buffer / atomic write | 64 KiB / 4096 bytes |
| PTY input, output and echo | 64 KiB each; empty EOF records also consume input budget |
| Nested signal frames | 8 |
| Program headers | 1024 per ELF |
| Symlink traversal | 40; targets at most 4095 bytes |
| Rootfs compressed / expanded input | 16 MiB / 128 MiB, pinned SHA-256 |
| Rootfs entries / individual file | 16,384 / 64 MiB |
| Initial stack | 64 KiB, non-executable, unmapped page below it |
| argv/envp entries combined | 4096 |
| Virtual file data (all live files) | Disk: 16 GiB initial logical budget, configurable and volume-bounded; memory: 16 MiB |
| VFS live nodes / namespace entries | Disk: 16,384; memory: 1024 |
| Serialized manifest input | 128 MiB, checksummed and structurally validated |
| Import/export buffer | 64 KiB |
| File descriptors | 256 per process |
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
plain console I/O, shared-VM threads, vfork, alternate signal stacks and blocking
futex remain unsupported rather than simulated as completed operations.
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
persisted. Unsupported raw/Unix sockets, ancillary messages, socket timeouts and
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
