# Delivery roadmap

M0, the selected static-userspace M1 foundation, M2 ash/FP and the storage follow-up are implemented, with records
in [m0.md](../validation/m0.md), [m1.md](../validation/m1.md), [m2.md](../validation/m2.md) and [storage.md](../validation/storage.md). This does not
establish general Linux userspace compatibility or an iPadOS build.

| Milestone | Work and completion evidence |
| --- | --- |
| M0 | Workspace, ELF, memory, reference interpreter, write/exit, static fixtures, QEMU comparisons, Linux CI |
| M1 | Delivered: wider integer/memory coverage, bounded VFS and ABI; 12 selected BusyBox applets and 18 comparison cases |
| M2 | Delivered: COW virtual processes, execve/wait, pipes, signal frames, PTY/job control; scalar/SIMD FP and interactive static ash |
| Storage | Delivered: private disk objects, atomic manifest, 16 GiB initial budget, volume-capacity API and CLI auto mode, streamed import/export and recovery validation |
| M3 | PT_INTERP and musl dynamic linking, verified Alpine rootfs, networking, apk; execute installed binaries |
| M4 | SwiftUI terminal, native SSH/SFTP, Files and keyboard; signed build and actual iPad validation |
| M5 | Versioned Bridge RPC and C ABI, scoped storage, accelerator registry and profiled C++ backend |
| M6 | Verified Git/Python/GCC/Clang/Make/CMake workflows; Debian after ABI coverage supports it |

Next is M3: dynamic linking and an independently verified Alpine rootfs, followed
by networking and package execution. The owner subsequently authorized storage:
the Linux CLI now defaults to a disk-backed store, with an initial 16 GiB logical
budget and capacity snapshots. VolumeSpace and stream APIs let the future iPad
frontend report native capacity and coordinate authorized Files resources. Native
Files UI, security-scoped bookmarks and provider coordination still belong to M4/M5.
Mapped-memory quotas remain unchanged. Installing a package is not proof that its programs can execute.

Repository policy clarification: the original charter proposed confirming private
visibility. During kickoff the owner explicitly selected public `kb5000/trames`,
overriding that proposal, and requested a GNU license. The initial concrete
license choice is GPL-3.0-or-later. License compatibility is reviewed separately
for each future dependency.
