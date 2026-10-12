# Delivery roadmap

The current verified release is [0.10.0 complete hybrid musl](../validation/release-0.10.md),
with one Rust cached interpreter, static ARM64 compute and recoverable guest CPU faults.
The C++ executor and tail-dispatch prototype have been removed.
The [source follow-up](../validation/compatibility-log.md) adds bounded runtime
logs and selected file syscalls; [shared mapping/ABI work](../validation/mmap-coverage.md)
adds coherent mappings, memfd and further file/scheduler entries included in 0.9.0. The canonical
[current CPU/Linux gaps](compatibility-current.md) links regenerated instruction
and Linux v6.12 syscall inventories, distinguishing missing entries from partial semantics.
[M6 compatibility](compatibility-m6.md) records the implemented Linux ABI subsets.
The [0.3 release](compatibility-gaps.md), [pre-implementation worktree](compatibility-gaps-worktree.md),
[0.4 profile](compatibility-0.4.md) and [0.7 performance](../validation/nonjit-0.7.md)
remain historical evidence for their own revisions.

The owner-provided [Trames Accelerator RFC](../architecture/accelerator.md) describes
guest library stubs and precompiled native backends. Development now includes the
[NativeCall v1 pure profile](../architecture/accelerator-v1.md), the initial bounded
transport and five functions from phases A/B. Stateful libc, GPU/video/ML and JIT
remain proposed. Version 0.10.0/build 11 integrates the full shared musl ELF and
activates the pure profile in the iPad application.
The subsequent [musl optimization queue](musl-optimization.md) records direct
guest-page access, ten adapted functions, capability negotiation and the next
pure/stateful candidates. App signing is grouped at a later milestone.

M0, the selected static-userspace M1 foundation, M2 ash/FP, the storage follow-up and M3 are implemented, with records
in [m0.md](../validation/m0.md), [m1.md](../validation/m1.md), [m2.md](../validation/m2.md), [storage.md](../validation/storage.md) and [network.md](../validation/network.md) and [m3.md](../validation/m3.md). This does not
establish general Linux userspace compatibility. M4's software and unsigned
Apple builds are validated in [m4.md](../validation/m4.md). The owner confirmed
Windows/Sideloadly signing, installation and startup on a physical iPad on
2026-10-11; the remaining device checklist is still open.

| Milestone | Work and completion evidence |
| --- | --- |
| M0 | Workspace, ELF, memory, reference interpreter, write/exit, static fixtures, QEMU comparisons, Linux CI |
| M1 | Delivered: wider integer/memory coverage, bounded VFS and ABI; 12 selected BusyBox applets and 18 comparison cases |
| M2 | Delivered: COW virtual processes, execve/wait, pipes, signal frames, PTY/job control; scalar/SIMD FP and interactive static ash |
| Storage | Delivered: private disk objects, atomic manifest, 16 GiB initial budget, volume-capacity API and CLI auto mode, streamed import/export and recovery validation |
| Network | Delivered M3 subset: nonblocking IPv4/IPv6 TCP/UDP, shared descriptors, real musl DNS including TCP fallback, 36 QEMU comparisons and iOS target compile check |
| M3 | Delivered: PT_INTERP, dynamic PIE/ET_EXEC, file-backed private mmap, verified Alpine 3.22.6, authenticated HTTPS apk update/add, successful trigger and installed tree execution |
| Virtual network (after M3) | Delivered: isolated IPv4/IPv6 and loopback, smoltcp TCP/UDP/ICMP packets, bounded two-family fragmentation, UDP/TCP DNS, native egress gateway, 27 QEMU comparisons and actual Alpine HTTPS apk |
| M4 | Software validated: SwiftUI/SwiftTerm, native SSH/SFTP, Files, keyboard and embedding ABI; Apple builds and simulator operations passed. Owner-confirmed Sideloadly installation/startup; remaining physical acceptance in issue #8 |
| Linux compatibility, 0.2.0 | Runtime proc/sys/dev views, virtual ownership and v3 persistence migration, musl threads/futex/timers, read-only route netlink, ICMP raw guest packets and native datagram echo egress, actual Alpine tools and default Vim editing; [record](../validation/linux-compat.md) |
| M5 | Software validated: versioned Bridge RPC, native consent, persistent scoped Files, built-in accelerator registry, optional profiled C++ backend and embedding ABI 2; signed-provider/device acceptance remains open |
| CPU reference, 0.4.0 | Broad AArch64 EL0/NEON and tested optional extensions, shared auxv/proc capability profile, standard root/device/interface names, checked memory/decoder acceleration; [scope](compatibility-0.4.md), [validation](../validation/reference-0.4.md) |
| Linux ABI/logging, 0.9.0 | Combined shared mmap/memfd, file/scheduler interfaces and dmesg; [release record](../validation/release-0.9.md) |
| CPU/native compute, 0.8.0 | Single Rust cached interpreter, ABI 3, native optional compute, expanded EL0 instructions and recoverable synchronous faults; [validation](../validation/instruction-coverage.md) |
| M6 | Event descriptors, local IPC, record locks/FIFOs, credentials and proc fixes; actual Git/Python/GCC/Clang/Make/CMake workflows; [validation](../validation/m6.md). Debian after ABI coverage supports it |

The owner requested on 2026-10-11 that future feature changes be grouped into a
larger release, with M5 explained before implementation. Keep the app's bundle
identifier stable across updates; internal CI builds do not require a new device
installation. The terminal viewport fix will also be included in that release.
Version 0.2.0/build 3 delivered Linux compatibility and the viewport fix. The
0.3.0/build 4 follow-up implements M5 with package-creation fixes and an explicit
Linux reset. Software verification is recorded in [M5](../validation/m5.md).

M3 is validated with unmodified Alpine binaries, a deterministic native HTTPS
peer and upstream Alpine-signed index/packages. The subsequent virtual network
is validated in [virtual-network.md](../validation/virtual-network.md).

The implemented virtual network reserves 10.0.0.1 for the
gateway/DNS and assigns environments 10.0.0.2, 10.0.0.3, etc. Internal addressing
and service ports are independent of native interfaces. A bounded userspace
packet stack handles internal protocols; native TCP/UDP supplies Internet egress
within host policy. No host route changes or VPN entitlement are required.

The Linux CLI now defaults to a disk-backed store, with an initial 16 GiB logical
budget and capacity snapshots. VolumeSpace and stream APIs now supply the iPad
frontend's capacity display and coordinated, security-scoped Files copies.
M5 adds persistent provider bookmarks; selected resources are not live guest mounts.
M6 permits 512 MiB mapped per process and 1 GiB across address spaces for LLVM
and compiler subprocesses. Installing a package is not proof that its programs can execute.

Repository policy clarification: the original charter proposed confirming private
visibility. During kickoff the owner explicitly selected public `kb5000/trames`,
overriding that proposal, and requested a GNU license. The initial concrete
license choice is GPL-3.0-or-later. License compatibility is reviewed separately
for each future dependency.

Version 0.10.0 integrates a [complete hybrid musl](../architecture/musl-hybrid.md) built from the pinned
original source, installed at normal libc/loader filenames. Adapted pure functions
use NativeCall; the remaining original libc/loader functions keep running as guest
code. The separate pure-profile library remains the initial validation tool.
Stateful allocator, TLS and I/O profiles follow after the full ELF integration.
