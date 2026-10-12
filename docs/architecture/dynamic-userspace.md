# Dynamic AArch64 Linux userspace

The ELF loader accepts checked ELF64 little-endian AArch64 ET_EXEC and ET_DYN
images. Main PIE images use a deterministic 0x40000000 load bias; an ET_DYN
interpreter uses 0x70000000. ET_EXEC retains its virtual addresses. PT_LOAD
ranges, permissions, file bounds, alignments and executable entry are checked;
page overlaps are rejected. PT_INTERP must be unique, absolute, NUL-terminated
and at most 4096 bytes. Its file comes exclusively from the guest VFS, with
execute permission; no corresponding host path is opened. Nested interpreters
and fixed-address ET_EXEC interpreters are rejected.

The initial PC is the interpreter's entry. AT_ENTRY and AT_PHDR/PHNUM describe
the main image; AT_BASE describes the interpreter. The main image's mapped end
sets the initial brk, independently of the interpreter and stack. Musl itself
loads dependencies, relocates symbols including RELR and TLSDESC, initializes
TLS and applies RELRO with mprotect. Trames does not emulate dynamic linking by
substituting library functions. An optional [Trames Accelerator design](accelerator.md)
is proposed separately and is not implemented. No host executable pages or JIT are involved.
Exec constructs the entire new image before closing CLOEXEC descriptors or
replacing the old process. Missing/unreadable interpreter errno is preserved.

The 0.9.0 mapping model provides coherent private/shared regular-file and anonymous
pages. Clean private pages read the file view; writes take per-page COW copies.
Shared fd/map aliases and forked shared mappings use the same inode page buffers;
msync flushes backing data. Full pages beyond EOF and truncation raise precise
SIGBUS, while partial last-page tails remain zero-filled without extending the
file. Mapped inodes survive close/unlink. Bounded imports, protections, remap,
quotas and cache invalidation are recorded in [shared mapping validation](../validation/mmap-coverage.md).

Symbolic links are byte strings in private inodes. Resolution follows at most
40 links, expands relative targets against the containing directory, roots
absolute targets in the guest namespace, and clamps .. at that root. No host
symlinks are followed. Intermediate links follow normally; lstat/readlink,
unlink, rename and O_NOFOLLOW treat the final link explicitly. Targets ending
in / require a directory. Disk manifest version 3 stores bounded link targets and virtual metadata.
Unsupported historical manifest versions are rejected; no migration is provided.

Advisory flock belongs to the shared open description: fork/dup share a lock,
and the last close releases it. Shared/exclusive conflicts return EAGAIN for
LOCK_NB or suspend a blocking virtual process; signals and SA_RESTART apply.
Lock conversion releases the old lock before competing for the new lock.
Host store exclusivity remains a separate native file lock. Guest fchdir uses
directory inode identity. Guest getrandom and stack AT_RANDOM use getrandom's
supported native entropy API, with checked guest buffers and a 1 MiB transfer
limit; no deterministic substitute supplies TLS entropy.

## Verified Alpine import

trames-distro accepts the pinned Alpine 3.22.6 AArch64 minirootfs SHA-256 recorded
in its source. scripts/prepare-alpine.sh obtains the same bytes through HTTPS
and checks that pin before preparing tests. The pin was checked against the
upstream HTTPS .sha256 publication; this is a digest trust anchor, not a claim
that a detached release signature was verified.

Compressed input is bounded at 16 MiB, expanded tar at 128 MiB, entries at
16384 and individual files at 64 MiB. All archive bytes, paths, types and quotas
are checked before namespace writes. Absolute member names, NUL, .. and
ambiguous duplicate paths are rejected; directories, regular files and symbolic
links are supported. Archive paths never reach tar host extraction APIs in the
product importer. Imported owners map to the current single virtual user (1000),
permission bits are preserved, and setuid/setgid/sticky bits are stripped.
Native device nodes are not imported; /dev/null is the existing virtual device.

The importer initializes a fresh namespace. An already installed distribution
is rejected; existing path conflicts are rejected before writes. A host IO
failure during disk import can leave a partial initialization; the caller must
use a new store for retry. Whole-store atomic upgrades, arbitrary rootfs archives,
package rollback and backup UI are later distribution-manager work.

Guest environment remains explicit. --guest loads a command from persistent
VFS; --rootfs initializes it from the verified archive; --env KEY=VALUE supplies
selected environment values. Host proxy credentials and environment secrets
are never automatically copied. Guest HTTPS and package authentication run in
unmodified Alpine OpenSSL/libfetch/apk, using guest CA files and Alpine keys.
The private storage grants no native root, mounts or privileged port capability.
