# 0.9.0 / build 10 — Linux ABI, shared mappings and runtime diagnostics

Release candidate, 2026-10-12. Linux and Apple CI, simulator results and unsigned
IPA verification are pending. Delivery links and immutable source evidence will
be added after both workflows succeed.

This combined update includes the file/log and shared-mapping batches following
0.8.0/build 9. It adds 28 numbered syscall entries in total: Linux v6.12 AArch64
now has 161 implemented subsets, 155 missing entries and two obsolete entries.

- Coherent shared file/anonymous mmap, fork, close/unlink lifetime, private COW,
  EOF/truncate SIGBUS with precise address, msync/mremap/mincore/madvise.
- memfd with classic seals, selected copy/vector/path/metadata and single-CPU
  scheduling APIs; existing quotas and unsupported flags are explicitly recorded.
- Session-scoped bounded `/dev/kmsg` and syslog; BusyBox dmesg displays unknown
  calls, guest arguments and CPU instruction/fault details.
- Guest instruction coverage remains the 0.8 arithmetic profile, with shared-file
  decoder-token invalidation extended for executable aliases.

Local implementation evidence: [shared mappings](mmap-coverage.md),
[diagnostics](compatibility-log.md), [current gaps](../roadmap/compatibility-current.md),
[syscall CSV](compatibility-current-syscalls.csv). CPU, numeric, network, package,
compiler and iPad frontend regression workflows must pass before delivery.

The owner-provided [Trames Accelerator RFC](../architecture/accelerator.md) is
included as a future proposal. The NativeCall instruction/protocol, guest libc
stubs, GPU/video/ML backends and a future optional JIT are not implemented by
this release. The existing interpreter and signed static ARM64 compute remain.

Installation will use Windows Sideloadly as before. Bundle ID
`org.trames.Trames`, embedding ABI 3 and disk format v3 remain unchanged. Use the
same Apple ID/effective signing bundle ID to retain Linux data. No data reset is
required for this update. Physical iPad acceptance remains with the owner.
