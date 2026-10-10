# Delivery roadmap

M0 is the Linux developer foundation, with the implementation and validation
record in [m0.md](../validation/m0.md). This does not establish general Linux
userspace compatibility or an iPadOS build.

| Milestone | Work and completion evidence |
| --- | --- |
| M0 | Workspace, ELF, memory, reference interpreter, write/exit, static fixtures, QEMU comparisons, Linux CI |
| M1 | Expand AArch64 coverage based on real static BusyBox traces; implement VFS and additional syscalls; execute selected applets |
| M2 | Virtual process model, execve, pipes, signals, PTY; interactive static ash |
| M3 | PT_INTERP and musl dynamic linking, verified Alpine rootfs, networking, apk; execute installed binaries |
| M4 | SwiftUI terminal, native SSH/SFTP, Files and keyboard; signed build and actual iPad validation |
| M5 | Versioned Bridge RPC and C ABI, scoped storage, accelerator registry and profiled C++ backend |
| M6 | Verified Git/Python/GCC/Clang/Make/CMake workflows; Debian after ABI coverage supports it |

Next implementation work is an M1 compatibility inventory: cross-build static
BusyBox, trace selected applets under QEMU, record actual instruction/syscall
requirements, and expand the differential corpus before implementing the gaps.
Installing a package does not count as proof that it can execute.

Repository policy clarification: the original charter proposed confirming private
visibility. During kickoff the owner explicitly selected public `kb5000/trames`,
overriding that proposal, and requested a GNU license. The initial concrete
license choice is GPL-3.0-or-later. License compatibility is reviewed separately
for each future dependency.
