# Runtime security and compatibility limits (M0–M2 and storage)

ELF files, instructions, addresses and argv/envp are untrusted. Parsers validate
lengths, offset arithmetic, architecture, flags, alignment, entry permissions and
segment ranges. Dynamic executables and overlapping PT_LOAD pages are explicitly
rejected in M0. Guest faults are errors, not host pointer dereferences.

| Resource | Current bound |
| --- | --- |
| Input ELF | 64 MiB (the CLI reads at most the limit plus one byte) |
| Mapped guest memory including stack | 64 MiB per process; 256 MiB aggregate logical mappings |
| Virtual processes including zombies | 32 |
| Pipe buffer / atomic write | 64 KiB / 4096 bytes |
| PTY input, output and echo | 64 KiB each; empty EOF records also consume input budget |
| Nested signal frames | 8 |
| Program headers | 1024 |
| Initial stack | 64 KiB, non-executable, unmapped page below it |
| argv/envp entries combined | 4096 |
| Virtual file data (all live files) | Disk: 16 GiB initial logical budget, configurable and volume-bounded; memory: 16 MiB |
| VFS live nodes / namespace entries | Disk: 16,384; memory: 1024 |
| Serialized manifest input | 128 MiB, checksummed and structurally validated |
| Import/export buffer | 64 KiB |
| File descriptors | 256 |
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
no symbolic links or general host directory mounts.
New file reads validate destination buffers before consuming positions. Vectored
output is preflighted and invokes the sink once; partial side effects are never
retried automatically. Anonymous fixed mappings are checked against resource
limits before replacing data. Real host entropy supplies AT_RANDOM.

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
