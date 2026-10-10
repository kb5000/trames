# Delivery roadmap

M0, the selected static-userspace M1 foundation, M2 ash/FP, the storage follow-up and M3 are implemented, with records
in [m0.md](../validation/m0.md), [m1.md](../validation/m1.md), [m2.md](../validation/m2.md), [storage.md](../validation/storage.md) and [network.md](../validation/network.md) and [m3.md](../validation/m3.md). This does not
establish general Linux userspace compatibility or an iPadOS build.

| Milestone | Work and completion evidence |
| --- | --- |
| M0 | Workspace, ELF, memory, reference interpreter, write/exit, static fixtures, QEMU comparisons, Linux CI |
| M1 | Delivered: wider integer/memory coverage, bounded VFS and ABI; 12 selected BusyBox applets and 18 comparison cases |
| M2 | Delivered: COW virtual processes, execve/wait, pipes, signal frames, PTY/job control; scalar/SIMD FP and interactive static ash |
| Storage | Delivered: private disk objects, atomic manifest, 16 GiB initial budget, volume-capacity API and CLI auto mode, streamed import/export and recovery validation |
| Network | Delivered M3 subset: nonblocking IPv4/IPv6 TCP/UDP, shared descriptors, real musl DNS including TCP fallback, 36 QEMU comparisons and iOS target compile check |
| M3 | Delivered: PT_INTERP, dynamic PIE/ET_EXEC, file-backed private mmap, verified Alpine 3.22.6, authenticated HTTPS apk update/add, successful trigger and installed tree execution |
| Virtual network (after M3) | Delivered: isolated IPv4/IPv6 and loopback, smoltcp TCP/UDP/ICMP packets, bounded two-family fragmentation, UDP/TCP DNS, native egress gateway, 27 QEMU comparisons and actual Alpine HTTPS apk |
| M4 | SwiftUI terminal, native SSH/SFTP, Files and keyboard; signed build and actual iPad validation |
| M5 | Versioned Bridge RPC and C ABI, scoped storage, accelerator registry and profiled C++ backend |
| M6 | Verified Git/Python/GCC/Clang/Make/CMake workflows; Debian after ABI coverage supports it |

M3 is validated with unmodified Alpine binaries, a deterministic native HTTPS
peer and upstream Alpine-signed index/packages. The subsequent virtual network
is validated in [virtual-network.md](../validation/virtual-network.md).

The implemented virtual network reserves 10.0.0.1 for the
gateway/DNS and assigns environments 10.0.0.2, 10.0.0.3, etc. Internal addressing
and service ports are independent of native interfaces. A bounded userspace
packet stack handles internal protocols; native TCP/UDP supplies Internet egress
within host policy. No host route changes or VPN entitlement are required.

The Linux CLI now defaults to a disk-backed store, with an initial 16 GiB logical
budget and capacity snapshots. VolumeSpace and stream APIs let the future iPad
frontend report native capacity and coordinate authorized Files resources. Native
Files UI, security-scoped bookmarks and provider coordination still belong to M4/M5.
Mapped-memory quotas remain unchanged. Installing a package is not proof that its programs can execute.

Repository policy clarification: the original charter proposed confirming private
visibility. During kickoff the owner explicitly selected public `kb5000/trames`,
overriding that proposal, and requested a GNU license. The initial concrete
license choice is GPL-3.0-or-later. License compatibility is reviewed separately
for each future dependency.
