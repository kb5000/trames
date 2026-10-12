# NativeCall v1 — initial pure-memory implementation

Development follow-up to the verified 0.9.0/build 10 source. This implements the
initial transport and pure functions from [RFC phases A/B](accelerator.md), under
[ADR-007](../decisions/ADR-007-nativecall-pure-profile.md). The immutable 0.9.0 IPA
predates this work. Embedding ABI 3 and Bridge SHA-256 registry ABI 1 stay unchanged.

## Enabled path

`RuntimeOptions { pure_accelerator: true }`, CLI `--accelerator pure`, or optional
embedding JSON `pure_accelerator: true` enables the profile for a session, including
fork/threads and exec. The default is false. The [complete hybrid musl integration](musl-hybrid.md) now supplies the iPad
resource and normal-path installation. Setting a host option alone still does
not install a guest library.

Build `libtrames-pure.so` with `scripts/build-accelerator-guest.sh`, using the
repository's pinned musl compiler. Install it in guest `/lib` and either call its
symbols through dlopen/dlsym or explicitly select it with guest
`LD_PRELOAD=/lib/libtrames-pure.so`. The guest loader performs ordinary linking.
This separate library is a temporary validation artifact. [Complete hybrid musl](musl-hybrid.md) now builds from pinned upstream source,
preserving ordinary
`libc.so` / `ld-musl-aarch64.so.1` paths and loader behavior: adapted pure routines
use NativeCall and all remaining libc/loader code retains the original guest
implementation. Installing that full ELF does not require native adaptation of
every function first. Stateful malloc/TLS/I/O adaptation follows incrementally,
with exact source/layout contracts. Static programs need rebuilding to adopt it.

The guest constructor reads private auxv key `0x54520001`; `0x10001` selects the
signed-in protocol 1/module 1. It preserves errno. On ordinary Linux/QEMU or a
host-disabled Trames session it uses the original guest implementation without
executing HLT. Activation is fixed at load time; hot switching is not supported.
The guest flag/manifest cannot enable a host-disabled CPU or grant host access.

## Exact first-profile contract

| Field | Meaning |
| --- | --- |
| x16 | `(protocol << 16) | module`, currently `0x10001` |
| x17 | 1 memcpy, 2 memmove, 3 memset, 4 memcmp, 5 strlen; full musl adds 6 memchr, 7 memrchr, 8 strnlen, 9 strcmp, 10 strncmp |
| x0/x1/x2 | Normal LP64 C arguments; memset takes the low unsigned byte |
| x0/x1 result | Internal aggregate: value, fallback (0 success / 1 original guest implementation) |
| Virtual encoding | `HLT #0x5452` (`0xd44a8a40`) |

`movz x16,#1; movk x16,#1,lsl#16; movz x17,#function; hlt#0x5452; ret`
is revalidated in currently executable guest memory on every entry. Invalid
profile/function/entry is SIGILL without memory effects. This catalog concerns a
small public pure ABI and exact stub words, not an exact full libc Build-ID or
internal-layout allowlist. The latter is still required before stateful profiles.
Untrusted guests may copy the valid stub; the dispatcher remains safe for such callers.

The CPU emits a typed exit and holds the HLT PC. Prior instructions are accounted
for separately. A committed result advances PC by four and retires exactly one
HLT; a backend memory fault leaves PC/input registers unchanged and uses existing
Linux signals with the precise guest data address. Callee-saved GPRs, SP, TLS,
flags and guest FP/vector state are retained. Calls are synchronous and at most
64 KiB of requested work; scheduling/cancellation resumes between calls. Unsupported
larger work uses a typed return, then the original interpreted guest function.

## Memory and original code

`trames-accel::native` validates guest memory in safe Rust. The development
follow-up uses scoped direct page slices for ordinary guest memory; directional
copy and host within-page memmove preserve overlap. Backed copies retain one
bounded source snapshot, and shared backing writes retain coherent staging.
This supports COW, shared aliases and code-cache invalidation. The complete
[optimization queue](../roadmap/musl-optimization.md) describes the direct and
buffered cases. New full-musl entries require function bits from private auxv
key `0x54520002`; old hosts without it retain original fallback for new calls.
Copies/fills preflight the complete bounded data ranges. Ordinary pointer/protection/
EOF faults therefore precede writes; backing I/O errors may expose prior page writes.
This does not promise the exact partial-store order of every upstream instruction.

memcmp/strlen scan within valid page boundaries and stop at a mismatch/NUL before
checking later pages. Invalid addresses produce SIGSEGV and file pages beyond EOF
produce SIGBUS. Return pointers retain tags; data accesses use the Memory TBI policy.
Zero-size operations do not dereference pointers; same-pointer memmove is a no-op.

The original musl 1.2.5 Arm memcpy/memset assembly is statically assembled into
`trames-native` for ARM64, with only macro/symbol/object-format adaptation, including Mach-O local-label
prefixes for ThinLTO. Safe
Rust translations retain musl unsigned comparison and HASZERO scan algorithms,
extended to bounded forward/reverse search and string comparisons. Development hosts
use Rust slice operations. Guest fallback compiles the original musl C and Arm
sources, privately renamed to avoid recursion. Original upstream bytes, archive
SHA-256, per-file fingerprints and full MIT/Arm notices are kept under
`crates/trames-native/upstream/musl-1.2.5`; the iPad app includes the notice.
The new wrapper and integration remain GPL-3.0-or-later.

## Validation and cost

[First implementation evidence](../validation/accelerator-v1.md) records actual
ARM64 comparisons, native canaries, protocol/budget/fault checks and the one-shot
benchmark. Native metrics report calls, fallbacks, rejected entries, faults, guest
bytes supplied and body nanoseconds. Current development also reports per-ID
entry counts and direct/buffered bytes. Total CLI CPU/wall metrics include entry
validation/dispatch. Historical first-bridge measurements include its temporary
buffers; follow-up measurements are recorded separately. No new benchmark is run on
every CI push. The existing Linux syscall and ordinary ISA counts are unchanged.
