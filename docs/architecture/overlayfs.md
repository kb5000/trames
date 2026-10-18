# Guest overlayfs

Trames implements `mount -t overlay` inside its own VFS. It does not mount the
host filesystem, forward host paths, or depend on Linux overlayfs being available
on iPadOS. Original Docker uses the `overlay2` graph driver and original containerd
can discover the `overlayfs` snapshotter. The latter discovery alone does not
validate containerd's complete snapshotter workflow.

## Layers and copy-up

`lowerdir=a:b` gives `a` precedence over `b`. Upper entries take precedence over
all lowers. Directory children merge; a regular file hides a lower subtree.
The merged filesystem owns a metadata/dentry view and references original lower
file inodes through `FileData::Lower`. Mounting and reading do not duplicate
lower file contents or charge their logical size again. Guest read syscalls still
use the ordinary bounded VFS buffers; this is not a general zero-copy IO claim.

On a permitted writable open, the selected lower file is copied to a new private
upper object in 64 KiB chunks (O_TRUNC discards data without copying it), then published along with its required parent
directories. Metadata changes also trigger copy-up (`metacopy=off`). New upper
inode IDs come from the backing filesystem allocator, so persistent inode/object
names cannot collide. Visible hard-link aliases within a view share copy-up.
Untouched siblings remain references to lower files.

Read descriptions and file mappings opened before copy-up retain the actual
lower data inode. New descriptions use upper data. Dup/fork retain the chosen data reference. A new proc-FD open selects the
current layer, just like reopening the overlay path; writable proc opens copy up. Shared
writable mappings use the copied upper object and existing mapped-page writeback.
Open-unlinked upper files keep their object and budget charge until last close.
Mount namespaces and bind mounts share the overlay superblock, without adding
CPU or worker locks.

## Whiteouts, opacity and persistence

Deletion of a name present below upper creates a `0:0` character-device whiteout
in upper. It is ordinary guest metadata, never a host device. A regular-file
`trusted.overlay.whiteout` (or `user.overlay.whiteout` with `userxattr`) is also
recognized when merging a layer. Deleting a lower-only nested child first copies
its parent directories; whiteouts cannot leave parentless manifest entries.

Removing and recreating a lower directory marks the new upper directory opaque
with `trusted.overlay.opaque=y` (or `user.overlay.opaque=y`). Its old lower
children stay hidden after unmount, remount and store reopen. Internal attributes
in a lower layer are consumed by merging rather than copied into ordinary
copy-up metadata. Trusted overlay attribute writes require guest CAP_SYS_ADMIN;
unprivileged attribute listings hide trusted names. Existing v4 manifests encode
the two trusted names with the same bounded attribute table; no new format or
historical migration is introduced. Older VFS builds accepting only `user.*`
can reject these stores; downgrading an active Docker store is not supported.
The iOS read-only provider already skips
bounded attribute payloads independent of their namespace.

An overlay namespace checkpoint stages the upper dentries, whiteouts, attributes,
link counts and inode references, then uses the backing filesystem's atomic
manifest replacement. Failed commits restore upper namespace/link state and the
calling VFS mutation restores its visible dentries/metadata. Unpublished copied
objects remain orphan-owned and are reclaimed when their last reference dies.
The next-inode counter can advance on a failed attempt; IDs are not reused.
Quota and attribute-budget failures are explicit. Durability retains the private
store's existing fsync/atomic-rename guarantees, not a new power-loss guarantee.

## Mount contract

Supported options:

- Required `lowerdir`, with up to 500 lower directories, leftmost highest.
- Optional paired `upperdir` and `workdir`, on the same backing filesystem and
  outside each other's subtree and every lower subtree.
- `userxattr`, `index=off`, `metacopy=off`, `redirect_dir=off|nofollow`, and
  `xino=off|auto` (automatic fallback; persistent lower inode IDs are not promised).
- Escaped `:`, `,` and `\\` in backing paths; paths resolve with ordinary guest
  symlink, cwd, mount and credential rules.

A lower-only mount is read-only and cannot be remounted writable. Readonly,
nodev/noexec/nosuid and DAC use existing VFS checks. `statfs` reports overlay magic
`0x794c7630` and upper capacity; `/proc/filesystems`, mountinfo and mounts report
`overlay` and its options. Inode identity is stable within the mounted view;
remounting does not promise persistent lower `st_ino` values.

The implementation creates an empty `workdir/work` directory, whose ownership
Moby adjusts after mount. Namespace staging uses the atomic private manifest
rather than host rename operations in that directory. An existing empty `work`
directory is reusable. Upper/work reuse or overlap while a mounted or retained
view is live returns EBUSY. Lazy detached trees are reaped before checking these
leases; live FD/cwd/root/executable/mapping references keep the lease.

Backing layers currently must be ordinary rootfs or tmpfs directories. Nested
overlay backing filesystems, `index=on`, `metacopy=on`, redirect directories,
fs-verity/NFS export and arbitrary new overlay mount API options are unsupported.
Renaming a lower/merged directory returns EXDEV (`redirect_dir=off`), allowing
userspace to copy the tree. Ordinary upper directory and file renames, replacements,
NOREPLACE, directory descriptors, and cross-mount EXDEV use existing VFS behavior.
Backing directories must not be edited through another path while mounted; the
merged dentry view is built at mount time. Remount after changing backing layers.

See [Docker usage](../guides/guest-docker.md) and
[storage acceptance](../validation/overlayfs.md).
