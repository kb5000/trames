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
SGID/set-ID behavior and real owner/group/other permission checks. Manifest v3 stores UID/GID. Version 0.3.0 accepts only v3; historical format migration was removed at the owner's request.
Complete timestamp, mount, locking, ACL and provider POSIX semantics remain
out of scope. Enabled BusyBox applets are validated only for the recorded cases.

The Docker compatibility batch writes manifest v4 with bounded inode-keyed `user.*`
attributes. Path, nofollow and fd set/get/list/remove share attributes across hard
links and open-unlinked references. CREATE/REPLACE, DAC, readonly mounts and sticky
directories are checked; failed quota or manifest commits restore attributes and
ctime. Limits: 128 names / 64 KiB per inode and 1 MiB per filesystem. File capability
and ACL writes return EOPNOTSUPP; their absent queries return ENODATA. A v3 store
opens with empty attributes and becomes v4 on its next checkpoint. Old binaries
cannot read v4. Decode validates names, inode types, duplicates and total budgets.

Named Unix sockets persist only their inode, mode and ownership (tag 8). Queue and
endpoint state belongs to the kernel session; reopening yields a stale endpoint
that returns ECONNREFUSED until unlinked and rebound. No host Unix path is opened.
`utimensat(AT_SYMLINK_NOFOLLOW)` now updates the symlink itself, including dangling
links unpacked by Docker's archive code.

## Capacity

New disk stores default to a 16 GiB logical file budget and 131,072 live inode /
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
loading of the main ELF still has its separate 256 MiB limit. M6 mapped-memory
bounds default to 512 MiB per process and 1 GiB aggregate and are now
[configurable](../guides/guest-memory.md). `--memory` retains the 16 MiB file and
1024 node/entry bounds; 16 GiB is never granted to that Vec-backed backend.

## Files interoperability

`import_reader` and `export_writer` accept platform-authorized streams. The Linux
CLI already copies selected native files using `--file` / `--exec-file` and
`--export`. Exports create a new destination and refuse existing files. Guest
modifications do not automatically change the source.

The Mac product also enables the shared runtime's optional `hostfs` backend.
`--mount GUEST=HOST` and `--mount-ro GUEST=HOST` attach an explicitly selected
native directory for live bidirectional file exchange. Open files retain their
original native inode across replacement, rename and unlink; bulk reads reuse
the disk pool. Data stays in the host filesystem and does not consume the private
store's logical-byte quota. Mounts are session-local. Ordinary guest paths do
not scan the native directory, synchronize a mirror or consult a host watcher.

`--fs ls/cat/get/put` provides explicit offline access to a private guest tree,
without creating a VM, presenter or server. Recursive copies use bounded 64 KiB
buffers. Existing destinations are preserved unless a regular-file upload uses
`put --replace`, which stages and atomically renames the new inode. The existing
exclusive store lock remains in effect; live exchange uses a mounted directory.

Host-file mmap, extended attributes and special files are not implemented.
External host changes are visible to normal file I/O and lookups, but are not
reported through guest inotify. The iPad product keeps its existing Files and
FileProvider path and does not enable hostfs by default. The Mac
[usage/limits](https://github.com/kb5000/trames-mac/blob/main/docs/guides/file-sharing.md)
and [native macOS/Linux benchmark record](../validation/hostfs.json) document
the measured scope. A guest pathname alone never grants access to a host path.

M6 persists named FIFO inode metadata as kind 5 in the current v3 manifest.
Its live bounded byte queue belongs to the virtual runtime and is recreated
empty after reopening a store. No host FIFO or device node is created.

For iPadOS, the app should keep its private store in the app container, report
the authorized volume's capacity, and wrap selected Files streams in the native
security-scope, coordination, cancellation and resource-release lifecycle.
M4 subsequently supplies native Files copying, C ABI plumbing and Apple builds;
the owner installed the first signed app. M5 adds persistent provider bookmarks;
physical provider acceptance remains pending. Copies remain separate guest data.

## Durability and evidence

fsync/fdatasync flush the selected inode and current namespace; sync flushes
all live files and the manifest directory. Normal CLI teardown requests sync
even after a runtime error. SIGKILL recovery is tested after a guest sync point.
The tests do not establish physical power-loss durability, APFS behavior,
provider consistency, device performance or a security audit. Only manifest v3 is accepted in 0.3.0.
There is no multi-instance concurrent-write protocol.

See [validation](../validation/storage.md) and [limits](../security/README.md).

Host file handles are opened for each inode IO operation and then closed. Guest
open-unlinked files retain the private object and store lock until their final
reference closes. The number of closed stored files does not consume host FDs.
Explicit native reset checks the lock before deleting an environment; it is not
a guest syscall or an automatic response to unsupported metadata.


## Guest overlay layers

The Docker storage batch adds [guest overlayfs](overlayfs.md). Merged views share
lower file data; writable opens and metadata modifications copy up into the
existing private upper store. Whiteouts and opaque-directory attributes survive
reopening the same store. The two supported `trusted.overlay.*` attributes extend
the existing bounded v4 attribute namespace without changing the manifest layout.
No host overlay mount is used. Layer contents and upper objects use existing
capacity accounting, inode lifetimes, fsync and manifest commits.
