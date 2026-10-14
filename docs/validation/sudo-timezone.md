# sudo PTYs and host timezone synchronization

Tracker: [Issue #19](https://github.com/kb5000/trames/issues/19).

The owner reported `sudo: unable to allocate pty: Not supported` on iPad.
The unmodified Alpine-signed sudo 1.9.17p2 reproduced the same failure: opening
`/dev/ptmx` returned EOPNOTSUPP. After enabling allocation, actual sudo additionally
required AF_UNIX `MSG_WAITALL` for its command-status records and wait4 `__WALL`
for non-PTY child reaping. All three paths are now implemented.

## Guest PTYs

- `/dev/ptmx` allocates an independent master/slave pair, initially locked;
  `TIOCGPTN`, `TIOCSPTLCK`, `TIOCGPTLCK` and slave opening support original musl
  `posix_openpt`, `ptsname`, `grantpt`, `unlockpt` and `openpty`.
- `/dev/pts/N`, fstat and proc fd links expose the allocated slave. Each pair has
  its own line discipline, termios, window size, queues, foreground group and
  session. The embedding terminal remains `/dev/pts/0`; allocated IDs start at 1.
- read/write/readv/writev, poll/select/epoll readiness and bounded backpressure
  use the shared line discipline. Basic TIOCPKT data headers and lock/packet
  queries are available; allocation state is independent of disk quotas.
- dup/fork aliases retain their endpoint until the last close. Buffered final
  slave output drains before master EIO; last-master close wakes slave readers,
  produces EOF/EIO/POLLHUP and sends terminal hangup signals. A live master permits
  reopening its unlocked slave.
- setsid detaches the old controlling terminal; session leaders can acquire an
  unowned slave on ordinary open without O_NOCTTY, or through TIOCSCTTY. `/dev/tty`
  follows the caller's controlling terminal. Proc stat records its terminal ID.
- Original syscall admission, credentials, sudoers and password checks remain
  active. No `!use_pty` workaround or patched sudo binary is used.

The finite AF_UNIX queue supports blocking MSG_WAITALL records up to 64 KiB,
without consuming a prefix before a scheduler retry. EOF and nonblocking reads
may return short data. Larger blocking WAITALL records return EOPNOTSUPP because
they require a consuming continuation. Existing nonzero VTIME and additional
terminal-driver ioctls remain outside this change; this is not a complete Linux
TTY driver implementation.

## Timezone

The Swift adapter captures `TimeZone.current` and its original `NSTimeZone.data`
TZif bytes. The guest receives `/etc/localtime`, `/etc/timezone` and the matching
`/usr/share/zoneinfo/Area/City` file. Original rules preserve historical offsets
and daylight saving transitions; a fixed current UTC offset would not do so.

Synchronization runs before launching a new local shell, after foreground resume,
and on the system timezone-change notification. Updates run on the existing guest
coordinator and atomically replace each changed file, with checked names and
bounded input. Identical files retain their inode/mtime. The update does not alter
guest credentials. Explicit guest `TZ` remains authoritative. Existing processes
retain normal musl timezone-cache semantics; newly executed programs see updated
default rules. No guest tzdata package download is required for the current zone.

## Verification

- 273 host Rust tests, strict Clippy, formatting and aarch64-apple-ios FFI
  compilation passed. 56 local Swift tests executed with no failures; the
  unrelated native SSH fixture was not supplied, so one test skipped.
- The static AArch64 PTY fixture matches native Linux and QEMU with 1 and 4 CPUs:
  allocation/locking, canonical/raw bytes, resize, independent pairs, packet
  data, alias lifetime, buffered close, EOF/EIO, reopen, controlling sessions,
  SIGHUP, split WAITALL records and __WALL. QEMU's ioctl translator omits
  TIOCGPTLCK (ENOTTY); only those queries skip in QEMU, while native Linux and
  Trames require them.
- Fresh signed sudo installation passes root/run-as identities, ordinary-user
  escalation, denied-user protection, actual passwd authentication with no
  password echo, nested sudo, interactive input/output, command exit 7, Ctrl-C
  exit 130, redirected execution and persistent reopening.
- The real embedding ABI passes initial Shanghai default, Unix epoch +0800,
  resync to New York, winter EST -0500/summer EDT -0400, named/POSIX TZ overrides
  and invalid-input preservation. Fresh signed Python 3.12.15 verifies localtime,
  datetime, DST, TZ override and store reopen.
- Existing ash PTY regression retains all 9 QEMU-matched editing/job-control
  outcomes. Existing embedding transfer, consent, pause/resume, cancellation and
  persistence tests pass. [Input/binary/log hashes](sudo-timezone-data.json) retain
  the concrete local evidence.

The [published 0.14.0/build 15](release-0.14.md) predates these changes. Its tag and
IPA remain immutable. This source update is intended for the next grouped device
release; actual iPad installation/notification acceptance remains to be checked.
