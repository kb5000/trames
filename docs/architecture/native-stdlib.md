# Native integer parsing and string operations

Development follow-up to 0.10.0/build 11. The complete musl still replaces the
ordinary `/lib/ld-musl-aarch64.so.1`, retains all public exports and original
fallback code, and uses the one cached interpreter without JIT.

## Original integer algorithm

Auxv `0x54520006 = 0x10004` advertises protocol 1/module 4, function 1. This separate
capability preserves the module-3 floating contract: an older numeric host never
executes an unknown integer entry. x0 is the source guest pointer, x1 a private
16-byte result frame, and x2 packs the unsigned 32-bit base plus signed flag in
bit 32. Other bits reject. The return holds the exact 64-bit integer and fallback
flag; the frame contains tagged end pointer and Linux errno (zero preserves errno).

Signed conversion uses musl's original limit `1ULL << 63`; unsigned uses `ULLONG_MAX`.
The unchanged pinned `intscan.c` is compiled into signed-in native code with only
its private symbol renamed. The existing bounded cursor and ASCII classifier
replace FILE and errno dependencies. No Darwin strtol, host locale, software
integer emulation or guest pointer cast is used. ARM64 long, long long and intmax_t
all have the required 64-bit width.

Public strtol, strtoul, strtoll, strtoull, strtoimax and strtoumax share this entry;
the six existing `__*_internal` aliases retain their exported names. Original
strtol.c is compiled with private symbol/weak-alias names for fallback. atoi,
atol, atoll, wide-character conversion and scanf's original intscan stay guest.

The floating module's page transport is reused: direct resident reads for a short
prefix, expanding borrows within a page, bounded gathering/replay across pages,
and original fallback at the 64 KiB ceiling. Delimiters/NUL stop before the next
unmapped page. Base 1, >36 and negative bases return EINVAL/end-at-source without
reading the source, including NULL. Shared/live output frames select original
before writes. The guest wrapper alone updates guest TLS errno and the caller's
end pointer in musl's original order. No host FP/errno state changes are needed
for the integer-only algorithm.

## String search and copy

Pure-module IDs 11–17 are strchr, strrchr, strchrnul, strcpy, stpcpy, strncpy and
stpncpy. The function bitmap is now `0x1ffff`; IDs 1–10 are unchanged. New wrappers
require their individual bit. Internal `__strchrnul`, `__stpcpy` and `__stpncpy`
entry points route the same operations, with the existing weak public aliases.

Searches inspect reached, checked page fragments using native word scans. strchr
stops on a match or NUL, strchrnul returns the first of those, and strrchr retains
the last unsigned-byte match before or at NUL. Returned pointers preserve guest
TBI tags. A bounded local prefix uses original musl's aligned-word HASZERO method; short
reverse searches use the pinned original memrchr after locating NUL.

Unbounded copies first scan through NUL, then preflight the entire destination.
Bounded copies read at most n bytes, copy the exact reached source, and zero-pad
through n after early NUL. strcpy/strncpy return the destination; stpcpy/stpncpy
return the terminator/truncation pointer. n=0 dereferences neither pointer. An
unbounded string reaching the ceiling, or n>64 KiB, falls back before effects.
Directly writable output is required; shared/live or write-only mappings select original before
writes. Original source objects and private fallbacks remain byte-for-byte pinned.

Copy/fill uses the existing checked memory API and original Arm native assembly.
COW, live EOF checks and executable-page decode-token invalidation retain their
existing hooks. No slice survives a native return. Whole-output preflight prevents
restarting an original copy after partial native mutation. This does not make C's
restrict-qualified overlapping copies defined.

Long strcpy/stpcpy inputs use native transport after a 64-byte local HASZERO
prefix; strncpy/stpncpy bounds below 128 stay local. Searches use a 64-byte local
prefix. These are current conservative policies, not universal speed guarantees.
Native work remains bounded and the existing cooperative return policy remains.

Metrics add integer entry/completion counts and extend per-pure-function counts
to 17. The existing numeric replay/input/result-byte counters now describe the
shared integer/floating scan transport; completed numeric_conversions continues
to count floats separately from integer_conversions. Supplied bytes are not exact
physical loads. Originals, MIT/Arm notices and hashes remain under the project's
GPL-3.0-or-later integration; the complete source package includes all guest patches.

[Validation and occasional application measurements](../validation/native-stdlib.md)
record the supported scope and measured limits.
