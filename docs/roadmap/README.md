# Delivery roadmap

The current instruction and syscall gaps are recorded in the
[0.3.0 CPU/Linux ABI audit](compatibility-gaps.md), including partial semantics,
representative instruction probes and the complete Linux 6.12 AArch64 number
inventory. This audit changes documentation only.

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
| M6 | Verified Git/Python/GCC/Clang/Make/CMake workflows; Debian after ABI coverage supports it |

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
Mapped-memory quotas remain unchanged. Installing a package is not proof that its programs can execute.

Repository policy clarification: the original charter proposed confirming private
visibility. During kickoff the owner explicitly selected public `kb5000/trames`,
overriding that proposal, and requested a GNU license. The initial concrete
license choice is GPL-3.0-or-later. License compatibility is reviewed separately
for each future dependency.
