# Static interpreter and native compute

The runtime has one Rust interpreter with bounded, data-only lazily decoded
AArch64 code pages. The single-step decoder is also exercised as a test oracle;
there is no production backend selector. The optional C++ executor and the
unsuccessful tail-dispatch experiment have been removed. No path allocates
executable memory, emits native code, or needs a JIT entitlement.

Both the Linux guest ABI and the shipping iPad host are ARM64. The x86_64 code in
`trames-compute` supports the current Linux development/CI host only: it does not
decode x86 guest instructions, and conditional compilation excludes it from the
ARM64 app. A separate ARM64 Linux build runs under QEMU to execute and check the
actual ARM64 helper instructions, rather than only compiling them.

## Native arithmetic

`trames-compute` accepts numeric values, never guest pointers. It uses static ARM
floating-point, NEON, AES and SHA instructions where the host supports them;
optional FP16 and crypto families are checked before execution. Packed SIMD
operations use whole vectors rather than per-lane IEEE arithmetic. Some guest
operations require several host instructions. Instruction decoding, register
selection, guest memory, privilege checks and Linux behavior still require the
interpreter: native arithmetic is not a direct guest-code execution mode.

An execution slice installs supported guest FPCR/FPSR controls once and restores
the caller's floating-point state on return, faults and unwinding. A scope cannot
move between threads. Guest FPCR/FPSR writes and reads synchronize native state,
including clearing sticky flags. Unsupported host operations and format conversion
retain bounded semantic fallbacks. Native execution takes priority over software
reproduction of every rounding, NaN payload and exception detail; the x86_64 test
host's floating-point flags are not an ARM64 conformance claim. ARM64 differential
checks still compare value and state snapshots against the independent oracle.

## Decoded execution

Each CPU owns 128 code-page slots arranged as 64 two-way sets, each containing at most
1,024 generation-tagged decoded instructions. The full virtual page number is hashed;
the most recently used way is probed first and the least recent way is replaced.
replacing one slot advances its generation without allocating or clearing the
backing array. Generation wrap explicitly clears tags before reuse. Only reached PCs are fetched
and decoded, so cold or inaccessible instructions are never speculatively read.
Execution stays in the selected page across branches and reference fallbacks,
looking up a page again only at a page transition, scheduler/syscall boundary,
or code-epoch change. This removes per-basic-block lookup and repeated prefix
allocation. Fork copies architectural state and starts with an empty cache.

Common arithmetic, logical masks, bitfields, conditionals, shifts, multiply/divide,
and scalar/vector loads and stores are decoded once. Immediates, scaled offsets,
register roles and reserved-field validation are cached. Other instructions use
the instruction semantics without copying CPU state or crossing an FFI boundary.
Hot entries are borrowed rather than copied by value. The decoded executor is
explicitly inlined at its single dispatch call site per memory specialization,
so inlining small ALU helpers cannot make LLVM outline the entire executor and
introduce an ABI call for every guest instruction. Register/width helpers use
inline hints; arithmetic and condition helpers expose constant modes to callers.
FRINTTS and LRCPC2 remain supported; the single virtual CPU advertises the tested
`frint` and `ilrcpc` features. The native compute helper is not an execution backend.

An executed flags-setting ADD/SUB (including CMP aliases) followed by an executed
conditional branch can form a two-instruction operation within the same page.
This combines dispatch and PC updates without allocating native code. It preserves
both register and NZCV results and counts both guest instructions. With only one
instruction left in a slice, it executes the arithmetic alone; the branch is
reached in the next slice. Cold words are fetched only when reached. No pair spans
a page or includes a memory operation, faulting instruction, or syscall.

Flag-setting ADD/SUB, including CMP/CMN and the arithmetic arm of CCMP/CCMN,
retain masked operands for deferred C/V. N/Z remain cheap result predicates so
EQ/NE/MI/PL can read them without checking a deferred-operation tag. Carry and
overflow are computed only when needed; subtraction's signed ordering compares
the saved operands directly. AL/NV inspect no flags. Non-S arithmetic preserves
the pending state, while an overwriting producer discards pending C/V without
evaluation. Logical operations, ADC/SBC results, FP comparisons and explicit
flag writes retain concrete NZCV.

Non-S ADD/SUB immediate have a separate decoded operation. SUB's immediate is
negated at decode time, leaving one wrapping add and destination-width truncation
in the hot path, with no flag-setting or subtraction-mode branch. SP/WSP roles
are preserved. Flag-setting forms retain their original operands and fusion.

Deferred flags are owned architectural CPU state, with no guest memory references
or host condition-register lifetime assumptions. They survive a slice, syscall,
CPU clone and worker migration. Public `flags()`/`nzcv()` reads return complete
architectural values; callers replace them through `set_flags()`/`set_nzcv()`.
The cached loop carries a separate local flag state, publishing it to `Cpu` on
every slice exit and before a reference fallback, then reading back fallback
changes. Common handlers need not repeatedly expose it through the register file.
ADC/SBC reads deferred carry before replacing any flags; RMIF, CFINV, SETF8/16
and FlagM2 preserve the required old bits. Signal frames materialize NZCV and
rt_sigreturn replaces pending state with the edited frame value. No per-slice
materialization or extra synchronization is needed.

A code page is keyed by address-space identity, virtual base and page code token.
Independent spaces cannot reuse code merely because their revision counters
match. Mapping/replacement/protection changes and successful stores to executable
pages invalidate only the affected page tokens. Other pages retain decoded entries.
The global code epoch remains a cheap invalidation signal: a previously checked
page needs no translation check until that epoch changes. On a change, the reached
page's execute permission and token are revalidated before reuse. A possible code store checks the epoch before the
next cached instruction, including self-modification inside a page. CLONE_VM
threads observe the same identity, epoch, mappings and bytes. Ordinary data
stores preserve decoded pages.
Global epoch wrap assigns a fresh address-space identity before repeated page
tokens can expose old decoded words. Per-slot generation wrap clears entry tags.

Execution reports a fault only at the PC actually reached, checks branch-target
alignment even within a warmed page, preserves the single-step fault state, stops at
SVC, and respects exact instruction budgets. The 4,096-instruction scheduler
quantum, signal boundaries, cancellation, PTY behavior and virtual clocks remain
in force.

## Guest memory

An ordered sparse map retains mmap/range/procfs behavior. A reusable frame slab
and a 256-entry software translation cache make repeated page lookup constant
time on a hit. Cache entries contain only a page tag and a stable frame index:
they own no page bytes, expose no native guest pointers, and add no owning reference
that could cause false COW. Unmapped frames release their bytes and are reused;
metadata is bounded by peak mapped pages rather than mapping-operation count.
Coordinator-local pages use Rc for COW; worker snapshots own separate concurrent
Arc pages. See [ownership and synchronization](region-memory.md).

Data permissions are checked on every access; execute rights are preserved by
the code-cache identity/epoch checks. Mapping/protection changes invalidate
translations in the shared space, and fork gets an independent cache. Fixed-size
1/2/4/8/16/32-byte operands avoid variable-size copies. Cross-page reads and writes
still validate the whole range before modifying a destination, guest registers,
writeback base, memory, or exclusive/code revisions. C-string reads scan a checked
page segment at a time and retain their original limits and byte semantics.

## Runtime overhead

Virtual mounts are initialized once, so `/dev` and `/sys` work even when a process
never reads `/proc`. Subsequent complete process snapshots occur only when an
operation traverses procfs or reads an open proc descriptor. The path probe uses
the real resolver, including cwd, dirfd, symbolic-link aliases, and failed lookups
of newly created process directories. Reads of proc files still refresh live
process, mapping, descriptor, signal and CPU accounting information.

Disk durability, manifest format, quotas and host-handle policy are preserved.
This release does not trade fsync/recovery correctness for package-install speed.
The M5 descriptor-exhaustion fix remains in place.

## Functional extensions and faults

The expanded scalar, SIMD, crypto and memory profile is recorded in
[the instruction inventory](../validation/instruction-coverage.md). Optional
BF16/FHM/complex/SHA512/SHA3/SM3/SM4 compute uses static host instructions first.
Fallbacks retain functional execution; optional floating-point fallback rounding
is not guaranteed bit-identical to hardware.

MOPS copy/move/set stages use Option A. A main stage processes at most 64 KiB
before yielding at the same guest PC with its architectural progress registers.
The partially executed instruction is not retired or charged twice. Page-local
checked transfers permit restart after a data fault, and keep cancellation and
other guest threads responsive.

Data accesses ignore the top address byte; instruction fetch and mapping APIs
retain their address validation. PAC/BTI authentication and allocation-tag checks
are disabled in the explicit compatibility profile, while branch/link, load,
writeback and zeroing effects execute. LS64 transfers use normal virtual RAM;
physical Device-memory/ACCDATA semantics are not modeled. Those protection and
LS64 capabilities are not advertised as complete hardware features.

Undefined instructions, BRK/HLT, alignment and mapping/permission faults become
Linux SIGILL/SIGTRAP/SIGBUS/SIGSEGV with fault addresses in siginfo/ucontext.
Registered guest handlers may edit PC and return through rt_sigreturn.
Ignored or blocked synchronous faults terminate instead of looping indefinitely.

## Measurement and limits

`--metrics` reports total instructions/time, CPU slice time, syscall time, idle
time, syscall count, proc refresh count, scheduler slices, decoded-page lookups/misses and instruction decode
counts. Timings include host scheduling and are not hardware cycle counts.
Syscall timing is opt-in; counts and existing CPU accounting are bounded
scalars. CPU time includes cache preparation and instruction decoding. Total runtime time includes final
VFS sync; ELF/store loading precedes the runtime timer. The manual program
benchmark additionally measures process spawn to first visible Vim cursor, or
spawn to successful Python/compiler exit, so loading is included in those times.

Run [the comparison harness](../../tests/performance/execution.py) only for material
changes, using a preserved baseline binary and identical signed-package stores.
It alternates the preserved baseline binary and current interpreter order,
restores a fresh copy before every run, retains Vim's defaults/plugins/history,
and validates Python and GCC results. Timing has no CI pass/fail threshold.

For a physical iPad, Vim itself can record startup stages with
`vim --startuptime /tmp/vim-startup.log`; after quitting, inspect that file.
Host Linux throughput and simulator acceptance do not establish device speed.
The present results are a measured optimization series, not a proof of the
maximum possible non-JIT throughput. Further work should follow measured cache
misses, dispatch/operand costs and workload-specific syscalls; superinstructions,
additional static decoding and storage indexing remain possible.
