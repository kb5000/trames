# Coherent backing access and bounded bulk NativeCall

This follow-up retains the single cached interpreter and joined NativeCall
coordinator. No JIT, pointer-as-guest-address cast, worker-side Rc/VFS transfer,
or retained host slice is introduced.

PageBacking has optional object-safe scoped read/write callbacks. A supported
backing invokes exactly once while retaining its borrow; unsupported backing
returns false without effects and Memory retains its staged contract. Memory
checks the reached page, permission and live EOF before borrowing. Shared
anonymous backing updates its revision/global invalidation before mutable access;
file backing additionally marks dirty and updates inode mtime. Normal msync,
fd access, aliases, truncation and parallel snapshot refresh keep using those hooks.
Private file pages still detach before writes and retain live EOF checks.
Already detached private pages can copy directly; live backed moves retain a
complete bounded source snapshot to preserve non-virtual aliases.

Bulk memcpy/memmove/memset/memcmp/memchr/memrchr accepts at most 1 MiB.
The entire copy/fill ranges are preflighted; larger requests fall back before
effects. Compare/search stops at reached fragments. Work is still synchronous
at joined checkpoints, so cancellation and scheduling resume between calls.
Page fragmentation does not create arbitrary mid-call scheduler yields or an
unlimited continuation. A future continuation must preserve overlap direction
and avoid restarting partially completed moves. Unknown backing I/O failure may
retain earlier effects, consistent with the existing contract.

AArch64 baseline NEON searches and compares 16-byte checked fragments with
scalar exact-byte resolution and bounded tails. Unaligned loads stay inside the
slice. Non-AArch64 development hosts retain the existing word routines. Original
Arm memcpy/memset assembly remains in use, with musl/Arm notices retained.

Pure IDs 18/19 add strcat/strncat; feature bitmap is now 0x7ffff. Existing IDs,
activation key and profile stay unchanged. Wrappers keep short work local and
check their individual feature bit. Original upstream functions are privately
renamed and retained unchanged; old 17-function hosts and ordinary Linux use
them. Concatenation scans destination/source, preflights output, preserves tagged
return pointers and writes one final NUL without strncpy padding. Zero bounds
never access the source. Shared/live or unreadable output retains original
fallback before writes. Each string scan/input remains limited to 64 KiB.

[Validation and occasional measurement](../validation/bulk-native.md).
