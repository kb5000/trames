# Native floating conversion over guest memory

Owner-directed follow-up, 2026-10-13 (Asia/Shanghai). Public strtod/strtof, unchanged
atof and the existing musl `_l` aliases use the standard complete libc/loader path.
strtold and its aliases retain original binary128 guest code. No JIT or additional
interpreter is introduced. Existing pure acceleration opt-in also gates this module.

## Protocol and original source

Auxv `0x54520005 = 0x10003` advertises protocol 1/module 3. Its exact function-1
stub carries source address in x0, a private 16-byte guest result address in x1,
and precision in x2 (0 binary32, 1 binary64). Return x0 is the IEEE bit pattern;
x1 is the original-fallback flag. The result contains the tagged end pointer and
Linux errno, with zero meaning preserve the caller's errno.

The guest wrapper sets guest errno and stores a supplied end pointer after native
return, preserving original musl's ordering. It never passes a guest errno/locale
object to host libc. Original strtod.c is privately renamed for its float/double
fallbacks; original atof and locale wrappers remain unchanged. musl's `_l` wrappers
already ignore the locale argument and forward to these public functions.

The native build uses unchanged pinned floatscan.c plus a checked, deterministic
adaptation: long double becomes double, LDBL constants select DBL constants, the
one long-double literal is narrowed and long-double math helpers become double
helpers. This selects musl's existing 53-bit intermediate algorithm on all hosts;
ARM64 GNU binary128 software routines are not linked into this native parser.
Original scan/helper sources, fingerprints and complete MIT/Arm notices remain
vendored under the project's GPL-3.0-or-later integration.

The private cursor uses bounded indices and synthetic termination at the end of
a supplied fragment. It records whether more bytes were required, including
incomplete exponents, infinity and NaN payloads. It is neither a guest nor host
FILE. ASCII classification follows original musl without host locale state.

## Memory, state and limits

Short input borrows the checked resident guest page directly without input
allocation. A longer prefix on the same page expands the direct borrow. Across
pages, the module gathers at most 64 KiB and reruns the bounded original scan;
this staging is explicit. It never reads an unmapped following page after a
reached delimiter/NUL. Live backings keep coherent snapshots and EOF checks.

No borrowed slice or host pointer survives native return. Result storage is
preflighted as directly writable; shared/backed stack output selects original
conversion before writes. Ceiling fallback also precedes result/errno/end writes.
Signals, threads, fork and nested qsort comparators use ordinary guest execution
between native calls. There is no persistent host operation or handle.

Native arithmetic uses hardware binary64/binary32 at nearest rounding. Explicit
guest rounding directions select original conversion. Host errno, FP control and
exception status are saved/restored even on early parser exits. Guest FP exception
flags are not synthesized for the internal native arithmetic. The owner's native
FP speed preference applies; this does not claim a strict binary128/exception-flag
implementation. Differential checks allow one ULP for the nearest native value,
but require exact signs/zero/infinity classes, errno and end position.

Metrics separately count numeric calls, completed conversions, replays, supplied
input fragments and 16-byte result commits. Supplied-byte counters are not exact
physical loads. Work is bounded by the 64 KiB input window and fixed 53-bit scan
tables; replays also cost work. Existing instruction budgets retire one virtual
HLT per return, and the bounded immediate-return scheduling policy is retained.

The portable native build needs a C compiler and archiver. Apple targets use clang
and Mach-O archives; the freestanding private headers avoid requiring an Apple SDK
for Linux compile-only checks. Linux ARM64 tests use the cross compiler; real Apple
linking and simulator tests remain CI checks. Native double math resolves to the
target's system math library, with host state restored at the boundary.

Validation includes decimal/hex input, special and malformed tokens, limits,
subnormal/overflow edges, locale aliases, tagged pointers, guard/page boundaries,
non-nearest fallback, shared stacks, signals, threads, fork and native sort → guest
comparison → native conversion. The optional `tests/performance/numeric.py` runs
one alternating batch against the preceding published ARM64 runtime.

[Functional checks, measured gains and actual simulator evidence](../validation/native-numeric.md) record this batch.
