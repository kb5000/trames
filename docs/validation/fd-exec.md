# Anonymous ELF and descriptor execution

Tracker: [Issue #20](https://github.com/kb5000/trames/issues/20).

The owner's signed Python `memfd_create` / `subprocess.run(pass_fds=...)` lab
failed with ENOENT, and AArch64 `execveat` returned ENOSYS. Exec followed the
`/proc/self/fd/N` magic link's displayed `/memfd:name (deleted)` string as a
pathname, instead of referencing its open inode. The memfd label also already
contained `(deleted)`, so proc added the suffix twice.

A single `(deleted)` is normal Linux presentation for an inode without a named
directory entry. A memfd is anonymous from creation; an ordinary unlinked file
can also retain valid open descriptors. Neither annotation alone prevents ELF
execution. The closed-fd path does disappear after its descriptor is closed.

## Implementation

- Select the executable inode once through the shared VFS symlink traversal,
  retaining proc fd/exe references even through ordinary symlink aliases. Check
  execute permissions and file type on that inode. Loading reads the complete
  image independently of the open description's current read offset.
- Add AArch64 syscall 281 `execveat`: absolute and dirfd-relative paths,
  `AT_EMPTY_PATH`, final `AT_SYMLINK_NOFOLLOW`, and executable `O_PATH` references.
  Raw O_PATH data I/O, mmap, fchmod and fchown return EBADF; inode metadata through
  supported `*at(AT_EMPTY_PATH)` calls remains available. Original musl may
  deliberately fall back to proc paths after raw fchmod/fchown EBADF; that libc
  policy remains unchanged.
- Reuse the existing ELF/shebang loader, credential changes, exec transaction,
  thread teardown and private close-on-exec fd table. Failed exec leaves the
  current image and descriptors intact. `AT_EXECFN` records the actual supplied
  or `/dev/fd/N[/path]` filename independently of arbitrary `argv[0]`.
- An executable inode reference survives CLOEXEC closure. Proc snapshots use
  weak references to avoid retaining it after exit. `/proc/self/exe` remains
  readable/statable/executable while the image is alive, including memfd and
  unlinked regular images. Retained memfd bytes still count toward its existing
  aggregate 128 MiB RAM budget; this change does not change that budget.
- Match Linux's FD-script distinction: CLOEXEC ELF execution succeeds, while
  `execveat` through a CLOEXEC file/directory fd returns ENOENT for a shebang
  whose interpreter could not reopen the synthesized fd path.

This extends the existing virtual kernel; it does not introduce a full mount,
capability, ptrace or regular-file ETXTBSY implementation. O_PATH and exec retain
the existing virtual namespace and process-access boundaries.

## Verification

`scripts/build-fd-exec-fixtures.sh` builds the same C cases as static host ELF
and static AArch64 musl ELF. `tests/integration/fd-exec.py` compares native Linux
against fresh disk-backed Trames environments with 1 and 4 CPUs. All 23 cases
match, including anonymous and unlinked ELF, original musl fexecve, an EOF fd
cursor, inode permissions, O_PATH, relative dirfds, symlinks, error precedence,
AT_EXECFN, CLOEXEC, proc exe re-execution and script interpreters. Native Linux
is the fd execution oracle; this comparison does not depend on QEMU's foreign
execveat support. Raw syscalls distinguish kernel semantics from musl/glibc's
different fchmod/fchown fallbacks.

`tests/integration/memfd-python.py` installs unmodified signed Python 3.12.15 in
a fresh environment, selects the full hybrid musl and runs the owner's pattern
with the exact `Hello from ARM64 syscalls` output and five-second timeout. It
also tests Python `os.execve(fd)` through original libc fexecve and ENOENT after
closing the fd. All three checks pass.

The final host workspace passes 275 Rust tests, formatting, strict Clippy and
iOS-target FFI compilation. Existing signed sudo, shared/private mmap and memfd
seals, embedding transfer/consent/PTY persistence, runtime signals/resource
limits and guest-created PTY regressions pass. Concrete binary, input and log
hashes are retained in [fd-exec-data.json](fd-exec-data.json). iOS compilation is
not an Apple SDK link, simulator run or actual iPad acceptance.

The syscall inventory now records 165 implementation entries out of the 318
pinned Linux 6.12 AArch64 numbered entries. Entries describe supported subsets,
not complete Linux ABI conformance; all 153 undispatched entries still return
ENOSYS in the audit probe. Historical inventories remain unchanged.

## Earlier CI follow-up

Apple workflow [38579896565](https://github.com/kb5000/trames/actions/runs/38579896565)
passed for the preceding sudo/timezone source `817a8c7`. Linux workflow
[38579895402](https://github.com/kb5000/trames/actions/runs/38579895402) failed
because the PTY test could lose SIGHUP between `while (!hungup)` and `pause()`,
leaving QEMU waiting until the 30-second timeout. The fixture now blocks SIGHUP
before notifying the parent and waits with `sigsuspend`, atomically unblocking
and sleeping. Three full native Linux/QEMU/Trames 1/4-CPU rounds passed after
that change. No runtime retry or kernel synchronization workaround was added.
Full exact-source CI for this follow-up remains tracked on GitHub.

The published [0.14.0/build 15](release-0.14.md) IPA predates this fix. This work
joins sudo/timezone changes for the next grouped IPA; no new IPA is produced by
this source update. Actual device testing remains pending.
