# Complete hybrid musl — standard libc and loader paths

Version 0.10.0/build 11 integrates the initial [NativeCall pure profile](accelerator-v1.md)
into a complete musl 1.2.5 shared ELF built from the SHA-256-pinned upstream archive.
The guest uses `/lib/ld-musl-aarch64.so.1` and Alpine's existing
`/lib/libc.musl-aarch64.so.1` symlink. No LD_PRELOAD or separately named library
is required. The complete public export set is retained; private original
functions and NativeCall stubs are hidden. The loader still identifies itself
as musl 1.2.5.

## Startup and implementation

Original musl source supplies the allocator, TLS, pthreads, loader, I/O, math,
locale and all other libc functions. The 0.10.0 release wraps
memcpy/memmove/memset/memcmp/strlen. The development follow-up also wraps
memchr/memrchr/strnlen/strcmp/strncmp, with a function capability bitmap and
[direct guest-page access](../roadmap/musl-optimization.md).
Original fallback source bytes stay intact: selected object files
are privately renamed through a compilation header. The original Arm assembly
remains the memcpy/memset fallback.

Two small checked startup hooks call a hidden initializer: static __init_libc
after locating auxv, and dynamic __dls2b after self-relocation and before TLS
copying. The initializer scans raw auxv for private profile key 0x54520001.
It performs no libc calls, allocations, constructors, syscalls or errno/TLS
access. Early dynamic stage-2 calls see the zero-initialized flag and execute
the original implementation. The wrappers/initializer have no stack protector.

Known-size operations below 128 bytes stay in the original guest implementation.
Enabled strlen first scans a bounded 64-byte aligned-word prefix using musl's
HASZERO algorithm; an early NUL returns without host entry. Aligned words cannot
cross a page. Larger work uses NativeCall v1, including its 64 KiB limit and
typed original-code fallback. Disabled Trames sessions and ordinary Linux/QEMU
use original routines and never execute the virtual HLT.

This is a complete hybrid libc/loader; current development adapts ten pure functions. Stateful
native allocator/TLS/I/O profiles remain future work requiring exact layout and
build identity contracts. Statically linked existing programs retain their
embedded libc; replacing a shared library cannot accelerate them automatically.

## Installation and iPad integration

`trames-distro::install_musl` bounds input, validates a loadable AArch64 ET_DYN ELF
and the standard libc alias, and compares the current loader before writing.
New bytes are copied/synced into a sibling inode, then one atomic namespace
rename replaces the loader. Invalid input and staging-path conflicts preserve
the existing library. Identical bytes are a no-op. User files need no reset.
The library is replaced before guest startup, under the existing exclusive
environment-store ownership. This is library replacement, not a whole-rootfs
transaction or snapshot recovery mechanism.

CLI activation is explicit:

```sh
bash scripts/build-musl-hybrid.sh
target/release/trames-run --storage /path/to/environment \
  --rootfs target/alpine-download/alpine-minirootfs-3.22.6-aarch64.tar.gz \
  --musl-libc target/musl-hybrid/libc.so --accelerator pure --guest /bin/ash
# On later launches omit --rootfs. Use --accelerator off for original fallback.
```

Embedding ABI 3 accepts optional `musl_libc` and existing `pure_accelerator`
JSON options. Without these options its existing behavior is retained.
The iPad application bundles the full Linux ELF as a resource and supplies both
options for installation/session startup. Xcode does not link or execute that
guest ELF. Apple CI first cross-builds it in a Linux prerequisite job, transfers
the same-workflow artifact, verifies its manifest digest, and copies it into
GuestMusl resources. The real Apple DNS/HTTPS simulator probe uses this path.

Each application launch ensures the bundled library is installed, preserving
the standard alias and user data. An apk musl upgrade can replace the loader
during a session; already loaded processes keep their mapped image and ordinary
new libc code remains supported. On the next application launch the supplied
bundled version is installed again. No APK database claims are rewritten.

## License, source and evidence

Trames adapters and integration are GPL-3.0-or-later; original musl/Arm MIT notices
are preserved. The library artifact includes the complete original source archive,
all adaptation/build files, GPL text, original COPYRIGHT, checked startup hashes,
compiler identity and the resulting ELF SHA-256. Pinned-source re-extraction on
each build avoids mixing stale patched/configured trees. [Validation and measured
limits](../validation/musl-hybrid.md) describe the actual tested scope.
