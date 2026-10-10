# M0 security and compatibility limits

ELF files, instructions, addresses and argv/envp are untrusted. Parsers validate
lengths, offset arithmetic, architecture, flags, alignment, entry permissions and
segment ranges. Dynamic executables and overlapping PT_LOAD pages are explicitly
rejected in M0. Guest faults are errors, not host pointer dereferences.

| Resource | M0 bound |
| --- | --- |
| Input ELF | 64 MiB (the CLI reads at most the limit plus one byte) |
| Mapped guest memory including stack | 64 MiB |
| Program headers | 1024 |
| Initial stack | 64 KiB, non-executable, unmapped page below it |
| argv/envp entries combined | 4096 |
| Guest address domain | below 2^48 |
| Single output syscall | at most 1 MiB copied; larger requests may return short writes |
| CLI default instruction budget | 10,000,000 |

These restrictions are deliberate M0 limits and can reject valid Linux programs.
Memory is not mapped to host executable pages. Code page permissions follow the
ELF; writable/executable guest segments are allowed, but remain interpreted data.

write validates the descriptor and guest address before output, including the
address domain for zero-length calls. Read access is required for non-empty
buffers. Output sinks are called once; interrupted or partial writes are returned
to the guest. CapturedOutput is a testing convenience with an unbounded Vec;
applications processing untrusted guests must provide a bounded output sink or
stream output as the CLI does. An instruction budget does not impose a wall-clock
or output quota on the embedding application's sink.

The runtime is not a hardware isolation boundary. Safe Rust and checked guest
memory reduce a class of bugs; parser and host integration defects can still
affect the containing app. No audit, broad parser fuzz campaign, sanitizers,
iPad device test, native SSH or host resource bridge has been completed in M0.
SSH host key verification and consent for sandbox resources remain required
when those subsystems are implemented.
