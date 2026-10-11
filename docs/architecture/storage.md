# Private disk storage

This follow-up implements persistence independently of M3's dynamic loader and
package work. The Linux CLI defaults to disk. Embedders explicitly attach
`Vfs::open_disk(path, DiskOptions)` to `Runtime::linux.vfs`; `Vfs::default()` is
still the bounded ephemeral test backend.

## Namespace and objects

An app-owned storage directory contains:

```text
.lock              advisory OS lock, held by the store and its open objects
manifest           versioned binary namespace, modes and timestamps
objects/<inode>.dat regular file contents; names are fixed-width hexadecimal IDs
```

Guest paths remain byte vectors in a shared virtual namespace. They never become
host path components. The manifest preserves non-UTF-8 paths, inode identity,
hard links, directories, 07777 permission bits and UID/GID and atime/mtime/ctime. Links are
reconstructed from validated namespace entries. `/dev/null` and the selected
virtual terminal remain runtime devices; no host device forwarding is added.

New content uses a private inode object. Namespace changes serialize a bounded
manifest to a new temporary file, flush it, and atomically rename it. The rename
is the commit point. A failure before it restores the in-memory namespace;
after it the new namespace remains authoritative. Directory durability is also
requested, and explicit sync operations report IO failures. The version-1 format
has an FNV-1a checksum for accidental corruption, not authentication. Unknown
versions, malformed structures, duplicate paths/inodes, invalid parents or modes,
oversized manifests and wrong object types fail before cleanup.

The store lock survives VFS process clones and open-unlinked files. A second
independent instance returns EBUSY. An OS process crash releases the lock without
a stale lock-file cleanup protocol. Reopening a valid store removes unreferenced
regular inode objects and an abandoned manifest temporary file. Unknown internal
names and symbolic links are rejected, rather than followed or deleted.

This is an app-private directory. The advisory lock coordinates participating
instances; it is not protection against hostile host directory replacement. New
Unix store directories and private files use 0700 and 0600. Do not place the
private object store directly in an externally writable file-provider folder.

## File and process semantics

Disk content uses ordinary Rust file IO with bounded transfers and sparse
`set_len`. Reads/writes seek to the guest open-description offset; pread/pwrite
leave that offset unchanged. Fork and dup share positions and status flags;
descriptor tables remain independent. Linux's O_APPEND behavior also applies
to pwrite. Truncation does not reset offsets, and a read beyond EOF keeps its
position. `stat` reports logical length and native allocated blocks on Unix.

Hard links share a single object. Removing the last link keeps an open file
readable and charged until the final description closes. Atomic file replacement
keeps old descriptions alive. Directory renames update path lookup by inode, so
other processes' cwd and directory descriptors follow the moved subtree. A
removed cwd returns ENOENT. Rename supports normal replacement and NOREPLACE;
exchange and arbitrary cross-filesystem mounts are not implemented. M3 added
virtual symlinks and file-backed private mappings.

The explicit executable installer replaces all existing aliases in one commit,
while preserving old open descriptions. Import copies, in contrast, require a
new destination. Installer staging conservatively requires enough budget for
both old and new live file data. Parent directories created by an import can
remain if a later individual host operation fails.

The guest ABI adds statfs/fstatfs, truncate/ftruncate, pread64/pwrite64,
fsync/fdatasync/sync, linkat, renameat/selected renameat2, fchmod/fchmodat and
utimensat. The 0.2.0 follow-up adds virtual UID/GID/ownership syscalls, sticky/
SGID/set-ID behavior and real owner/group/other permission checks. Existing v1/v2
metadata keeps owner 1000; v3 stores UID/GID without discarding guest data.
Complete timestamp, mount, locking, ACL/xattr and provider POSIX semantics remain
out of scope. Enabled BusyBox applets are validated only for the recorded cases.

## Capacity

New disk stores default to a 16 GiB logical file budget and 16,384 live inode /
namespace-entry limits. Existing stores keep their recorded limits unless the
embedding app or CLI explicitly changes them. `--storage-budget 16GiB` accepts
bytes or integer KiB/MiB/GiB/TiB units. `--storage-budget auto` records the selected
volume's total capacity. The Linux CLI obtains volume figures from native `df`;
the runtime itself does not spawn commands to read capacity.

`VolumeSpace { total_bytes, available_bytes }` is supplied by the platform.
Effective budget is bounded by total volume capacity. Available growth is the
smaller of logical budget headroom and the reported available-space snapshot,
adjusted for subsequent virtual file growth/shrink. Platforms can refresh the
snapshot with `set_volume_space`. This is not an allocation or reservation; real
host IO can still fail if other applications use space.

Logical lengths, including holes and open-unlinked files, count once per inode.
Aliases add no content charge. Large ordinary files are not loaded wholesale:
guest transfers are at most 1 MiB and import/export buffers are 64 KiB. Executable
loading still has its separate 64 MiB limit. Guest mapped-memory bounds remain
64 MiB per process and 256 MiB aggregate. `--memory` retains the 16 MiB file and
1024 node/entry bounds; 16 GiB is never granted to that Vec-backed backend.

## Files interoperability

`import_reader` and `export_writer` accept platform-authorized streams. The Linux
CLI already copies selected native files using `--file` / `--exec-file` and
`--export`. Exports create a new destination and refuse existing files. Guest
modifications do not automatically change the source. No general host directory
mount or access inferred from a guest path is provided.

For iPadOS, the app should keep its private store in the app container, report
the authorized volume's capacity, and wrap selected Files streams in the native
security-scope, coordination, cancellation and resource-release lifecycle.
M4 subsequently supplies native Files copying, C ABI plumbing and Apple builds;
the owner installed the first signed app. Persistent provider bookmarks and
complete provider behavior remain pending. Copies remain separate guest data.

## Durability and evidence

fsync/fdatasync flush the selected inode and current namespace; sync flushes
all live files and the manifest directory. Normal CLI teardown requests sync
even after a runtime error. SIGKILL recovery is tested after a guest sync point.
The tests do not establish physical power-loss durability, APFS behavior,
provider consistency, device performance or a security audit. Manifest v1/v2 recovery and migration to ownership-bearing v3 are implemented.
There is no multi-instance concurrent-write protocol.

See [validation](../validation/storage.md) and [limits](../security/README.md).
