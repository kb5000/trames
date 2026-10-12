# Guest callbacks and resumable native sorting

Owner-directed follow-up, 2026-10-13 (Asia/Shanghai). The callback transport uses
the existing single cached interpreter and precompiled native code, without JIT.
It extends [pure NativeCall](accelerator-v1.md); its new module does not change
the first ten functions or the embedding ABI 3.

## Protocol and ownership

Private auxv `0x54520003 = 0x10002` advertises protocol 1/module 2. Its one exact
stub uses `movz x16,#2; movk x16,#1,lsl#16; movz x17,#1; hlt#0x5452; ret`.
Every entry is rechecked in executable guest memory. x0 is a guest continuation
address, x1 the preceding signed 32-bit comparison result, x2 reserved zero.
The existing x0/x1 result aggregate carries an event and a fallback flag.
Events are 0 done, 1 guest comparison requested, 2 bounded native checkpoint.

The continuation is a private versioned LP64 record, version `0x54525301`.
Sort reserves 240 words on the guest stack; search reserves 16. All mutable
progress, array dimensions, pending arguments and return values are guest-owned.
No host sorting stack, handle registry, guest function pointer cast or page borrow
survives a step. The backend validates dimensions, indices, phase and memory on
each resume. Rebuilt native Leonardo numbers avoid trusting a guest table.
Only mutable header/path words are transported each time; the reserved table is
initialized once. The same fixed frame can be copied by ordinary guest COW.

The guest wrapper executes a normal function-pointer call when comparison is
requested, then invokes the next native step. This supports native sort → guest
comparison → native strcmp or another sort → another guest comparison. Native
array movement calls checked native copy helpers directly, without guest traps.
The comparator remains ordinary guest execution, including its TLS, errno,
signals, syscalls, blocking, threads and return convention.

fork inherits the guest stack and array progress through existing COW. exec drops
the old address space and keeps host routing options in its newly constructed
auxv. Signal handlers can start their own operation, including on an alternate
stack. longjmp/siglongjmp abandoning a wrapper leaves no host operation to resume
or free. This ownership mechanism avoids a second interpreter or recursive host
scheduler. It is not a universal stateful-native-library implementation.

## Original musl algorithms and ABI

The native Smoothsort state machine follows musl 1.2.5 qsort.c's build/extract,
sift, trinkle and 256-byte record cycles. The original source, atomic helper and
per-file archive fingerprints are vendored unchanged. Valentin Ochs' MIT-style
notice is retained alongside musl/Arm notices under GPL-3.0-or-later integration.
Original qsort.c/bsearch.c objects are privately renamed by a compile header;
source bytes are unchanged. The two-stage weak-alias macro expands the renamed
target before stringification.

musl qsort_nr.c remains the original code: its guest wrapper_cmp adapts qsort's
two-argument comparator to the three-argument __qsort_r/qsort_r driver. Thus both
public APIs use the same resumable algorithm, and qsort_r retains its GNU context
argument. bsearch follows the original midpoint and range-update sequence.
The public export set and standard libc/loader paths are retained.

## Limits, fallback and scheduling

Public sort routing starts at 32 elements. The first profile accepts at most
64 KiB of array data and directly writable anonymous/private COW bytes. Live
shared/backed arrays and read-only arrays select original musl before native
array writes; read-only already-sorted arrays retain original behavior. Private
file copies continue to check live EOF. Larger arrays also select original code.
An initially unsupported continuation backing, including a legitimate shared
pthread stack, also selects original code before array mutation.
After a native array modification there is no original-function restart.
Changing active array mappings into unsupported backing is rejected rather than
restarting a partially sorted array. Comparators must satisfy the standard sort
contract, including not modifying the array being sorted.

Each step bounds internal transitions and record movement. Rotations checkpoint
between complete 256-byte stripes when necessary. The context is preflighted as
directly writable; no backing I/O occurs while committing its progress. Actual
guest pointer/protection/EOF faults retain normal signal handling. No guest code
runs while native slices are borrowed.

Immediate successful native returns can reuse the remaining 4,096-instruction
quantum. A fault or syscall returns to the outer scheduler, including blocking
callbacks. At most 32 native returns or two milliseconds pass before the next
outer control/signal/timer sweep. The elapsed-time check runs after a guest slice
and native step, so their duration can exceed the time target; this is a
cooperative limit rather than a hard real-time deadline.
Native guest instruction retirement remains one HLT per committed result;
internal native work is bounded separately, not reported as fictitious guest
instructions. This removes repeated full scheduler sweeps at each comparison.

## bsearch routing and measurement

The native bsearch backend and nested guest driver are implemented. A diagnostic
4096-element integer lookup workload showed extra entry overhead compared with
its already short original loop. Default public bsearch therefore stays in
original musl. CLI `--accelerator pure --native-bsearch`, or optional embedding
JSON `native_bsearch: true` with pure acceleration enabled, explicitly tests
native routing. Auxv `0x54520004 = 1` advertises that routing choice; it persists
across exec. No iPad product toggle is added. This is a routing policy, not new
host privileges; all registered operations remain bounded guest-memory work.

Metrics add callback calls, requests, checkpoints, sorts/searches and bytes
transported for continuation state. Array movement retains direct byte counters.
Use the optional `tests/performance/callbacks.py` to compare the same full libc
and guest programs with acceleration off/on, and optionally a saved full-frame
continuation runtime.
Report comparator and serialization costs separately from end-to-end sort gains.
Numeric BusyBox sort still executes its application comparator and atof/strtod
as guest code. Native qsort alone does not establish a whole-command speedup.
See [functional checks and measured limits](../validation/native-callbacks.md).

Validation includes identical original/native comparator traces, varied record
widths and duplicates, nested sort/search/string calls, two guest threads,
alternate-stack signals, fork mid-comparison, exec, blocking pipe reads and
nonlocal exits. Malformed frames, limits and fallback-before-write are checked.
Remaining work includes larger cooperative arrays, native comparator/number
parsing paths, and further typed callback families such as allocator/stdio glue.
