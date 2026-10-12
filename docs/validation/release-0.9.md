# 0.9.0 / build 10 — Linux ABI, shared mappings and runtime diagnostics

Verified delivery, 2026-10-12. [Download the unsigned IPA artifact](https://github.com/kb5000/trames/actions/runs/38251377309/artifacts/11749317346), unzip
it and sign `Trames-unsigned.ipa` using Windows Sideloadly. Artifact retention is
14 days. Both workflows passed for the same source commit.

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
compiler and iPad frontend regression workflows passed before delivery.

The owner-provided [Trames Accelerator RFC](../architecture/accelerator.md) is
included as a future proposal. The NativeCall instruction/protocol, guest libc
stubs, GPU/video/ML backends and a future optional JIT are not implemented by
this release. The existing interpreter and signed static ARM64 compute remain.

Installation will use Windows Sideloadly as before. Bundle ID
`org.trames.Trames`, embedding ABI 3 and disk format v3 remain unchanged. Use the
same Apple ID/effective signing bundle ID to retain Linux data. No data reset is
required for this update. Physical iPad acceptance remains with the owner.

## Delivered evidence

- Runtime commit: [`3d8789d6e0c02c45949b4c314b1047f121e6f986`](https://github.com/kb5000/trames/tree/3d8789d6e0c02c45949b4c314b1047f121e6f986); tree `5337eb143b09df2b9eddaf8564a80aa313b75a4c`.
- [Linux CI](https://github.com/kb5000/trames/actions/runs/38251377312) and [Apple CI](https://github.com/kb5000/trames/actions/runs/38251377309): success; all required steps passed, including native Apple ARM64 memory/VFS/core tests, SSH/SFTP and the actual guest mapping/logging fixtures.
- Seven iPad simulator tests passed, zero failures/skips, including actual Alpine gateway DNS/HTTPS and a `/dev/kmsg` write read back through `dmesg`. This is simulator evidence, not a new physical-iPad result.
- IPA: ARM64 iOS device executable, version 0.9.0/build 10, iPadOS 17 minimum, executable permission retained; all 36 packaged files byte-match the actual built device app. ZIP integrity and SHA-256 sidecar verified.
- IPA size: 9473976 bytes; SHA-256 `8c03880e65ac2726c32a57c29ee7e03f1f4e9b7c993cebaffc6bf656456cf85b`.

The fresh signed developer-tool workflow also passed Python mmap/fork/resize and
SQLite mapping checks, adduser/su permissions, htop, Git, GCC/Clang and
CMake/ctest; its [runtime log](release-0.9-runtime.log) is preserved here. Large
package/compiler workflows use the Linux development-host executor. Strict CPU
and musl ABI comparisons execute actual AArch64 under the pinned QEMU oracle;
native Apple ARM64 and linked-app simulator tests provide separate Apple evidence.

Machine-readable records: [CI and artifacts](release-0.9-ci.json), [IPA verification](release-0.9-ipa.json), [actual simulator summary](release-0.9-simulator.json).
