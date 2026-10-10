# Reproducible-input BusyBox tests

`scripts/build-busybox.sh` downloads these fixed upstream archives over HTTPS and
checks the recorded SHA-256 before extracting or building:

| Source | SHA-256 |
| --- | --- |
| musl 1.2.5 | a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4 |
| BusyBox 1.37.0 | 3311dff32e746499f4df0d5df04d7eb396382d7e108bb9250e7b519b837043a4 |

BusyBox's published checksum agrees with the second value. This is archive
integrity checking, not an assertion that release signatures were verified.

The script builds static musl with an explicit GCC wrapper and uses BusyBox
allnoconfig plus twelve applets and their selected features. Source code is not
patched. Both builds use `-O2 -fno-tree-vectorize -fno-tree-slp-vectorize`; musl's
upstream AArch64 assembly memcpy/memset still execute normally. The retained
`.config` and unstripped executable are under `target/busybox-build`. Compiler
versions and build timestamps can change binary bytes; tests execute the same
built ELF on both sides instead of requiring a universal binary hash.

Tools can be overridden through AARCH64_CC, AARCH64_AR and AARCH64_RANLIB.
TRAMES_SOURCE_CACHE selects an archive cache, and TRAMES_BUILD_JOBS controls build
parallelism. This invokes compiler processes, not delegated agents. A checksum
failure stops the build. Build outputs and source archives are not committed.

Run `python3 tests/integration/busybox.py`. It asserts reference exit codes and
compares stdout/stderr/exit for eighteen cases. Reference file operations run
inside a temporary host directory; Trames imports matching bytes into its
virtual root. Only the reference pwd's known temporary directory prefix is
normalized to `/`. Directory names, file contents and error text are compared.
The independent host-scope check ensures naming an unimported host file does
not grant guest access.

Current tests use true, false, echo, printf string/integer formats, basename,
dirname, cat (including binary stdin and a missing file), wc, head, tail, pwd and
ls. No broad BusyBox configuration, floating-point printf, static ash or Alpine
rootfs compatibility is claimed. The floating-point printf probe currently
faults on FMOV of the upper D lane before broader FP requirements can be assessed.

BusyBox uses GPLv2 and musl uses MIT, with individual source notices preserved
in their downloaded source trees. These are separately built guest programs;
they are not linked into the GPL-3.0-or-later Rust runtime. The repository
supplies source URLs, complete test build configuration and commands. Any future
distribution of built guest binaries must preserve their corresponding licenses
and source obligations separately.
