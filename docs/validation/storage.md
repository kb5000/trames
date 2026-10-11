# Storage acceptance

The same static AArch64 binaries run in Trames and independent QEMU. Existing
M0–M2 comparisons explicitly select `--memory`; M2's complete shell/process/libm
and PTY tests also run with `TRAMES_TEST_STORAGE=disk`. All reference wrapper and
cwd normalization constraints from [M2](m2.md) still apply.

| Check | Evidence |
| --- | --- |
| Workspace | 77 unit tests; formatting; Clippy with warnings denied; locked build |
| Existing M0/M1/BusyBox/FP/structures/M2 | 427 QEMU program/scenario comparisons; 6,860 FP cases; 159 structure-memory variants |
| Disk M2 | 24 additional QEMU comparisons: 22 shell cases and independent process/libm C fixtures |
| Integer SIMD reductions | 48 64/128-bit signed/unsigned pairwise min/max snapshots, including destination aliases, in one comparison; reserved-size fault unit |
| Storage ABI | One independent static musl C comparison for sparse >20 MiB files, positional IO/offsets, allocated blocks, flush, links/permissions/timestamps, open unlink, rename replacement, truncation, append, fork cwd/dirfd and capacity |
| File applets | One shell comparison using cp/mv/ln/chmod/touch/dd/sync and file lifecycle; set -e requires every command to succeed |
| Total QEMU programs/scenarios | 454 comparisons; terminal outcomes counted separately |
| PTY | Both memory and disk backends match the same 9 semantic outcomes against QEMU ash |

The storage C compiler emits UMAXP for a fixed-size memcmp expansion even with
loop/SLP vectorization disabled. SMAXP/SMINP/UMAXP/UMINP are implemented and
differentially tested; neither the fixture nor upstream musl is patched to
replace the behavior with host execution.

Twelve storage units cover byte-path/hard-link/mode/time recovery; exact 16 GiB
sparse-file boundaries and allocated blocks; shared fork positions and
open-unlinked quota; store lock lifetime; capacity snapshot refresh; failed
namespace commits and rollback; orphan cleanup; checksum corruption without
cleanup; bounded >20 MiB stream copying; cross-process cwd/dirfd after subtree
rename; executable replacement with old descriptions; and symlinked objects /
dangling locks. The 16 GiB test uses logical holes and verifies small native
allocated size; it does not allocate 16 GiB of RAM or write 16 GiB of payload.

Storage integration also verifies independent CLI launches retain files and
append correctly; streamed binary import/copy/export matches SHA-256; custom
budgets survive reopening; the default store records 16 GiB; auto capacity agrees
with the reported native volume; neighboring unimported host files remain
inaccessible; exports refuse existing or invalid targets before creating a file;
instruction-budget errors retain earlier writes; and another instance cannot
acquire the active store. A host SIGKILL after guest sync retains the linked file
and leaves an open-unlinked inode that is reclaimed on the next valid reopen.

Reproduce using the commands in [README](../../README.md). BusyBox/musl archive
versions, digests and build flags remain pinned. Added applets do not imply that
all their options or arbitrary distribution binaries are compatible.

Scope is app-private storage on the tested Linux filesystem. No physical
power-loss test, hostile concurrent host mutation test, parser fuzz campaign,
audit, iPad/APFS/device run, native Files-provider integration, direct host mount,
symlinks, dynamic linking, networking or package execution is established.

This record describes the original milestone checks. The subsequent 0.2.0
[Linux compatibility record](linux-compat.md) adds runtime files, ownership
migration, shared-VM threads/futex/timers and ICMP/netlink support and replaces
those earlier unsupported-feature statements for the documented subset.
