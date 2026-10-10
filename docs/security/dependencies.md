# Network dependency license record

The direct native-network dependencies are pinned; Cargo.lock fixes transitive
versions and registry checksums. Reviewed package manifest license declarations:

| Crate | Locked version | Declared license |
| --- | --- | --- |
| socket2 | 0.6.1 | MIT OR Apache-2.0 |
| nix | 0.31.3 | MIT |
| libc | 0.2.190 | MIT OR Apache-2.0 |
| bitflags | 2.13.2 | MIT OR Apache-2.0 |
| cfg-if | 1.0.5 | MIT OR Apache-2.0 |
| cfg_aliases | 0.2.2 | MIT |

These selected MIT/Apache-2.0 terms are compatible with the project's
GPL-3.0-or-later license. Retain the upstream copyright and license notices when
distributing their source or compiled components; the Cargo registry packages
include them. Cargo.lock also contains socket2's Windows-target dependencies,
which are not compiled for the validated Linux/iOS targets. Windows is not a
supported Trames target.

This records license compatibility, not a security audit of dependencies. Native
FFI and platform-specific unsafe implementations live in the dependencies.
Trames crates continue to forbid unsafe code. GNU licensing does not automatically
make every possible library compatible; each added dependency needs its own
license review.

## M3 archive, digest and entropy dependencies

Reviewed Cargo manifest declarations for the additional Linux/iOS dependency
closure (all pinned by Cargo.lock):

| Crate | Version | Declared license |
| --- | --- | --- |
| flate2 | 1.1.10 | MIT OR Apache-2.0 |
| tar | 0.4.46 | MIT OR Apache-2.0 |
| sha2 | 0.10.9 | MIT OR Apache-2.0 |
| getrandom | 0.3.4 | MIT OR Apache-2.0 |
| adler2 | 2.0.1 | 0BSD OR MIT OR Apache-2.0 |
| block-buffer | 0.10.4 | MIT OR Apache-2.0 |
| cpufeatures | 0.2.17 | MIT OR Apache-2.0 |
| crc32fast | 1.5.2 | MIT OR Apache-2.0 |
| crypto-common | 0.1.7 | MIT OR Apache-2.0 |
| digest | 0.10.7 | MIT OR Apache-2.0 |
| filetime | 0.2.29 | MIT/Apache-2.0 |
| generic-array | 0.14.7 | MIT |
| miniz_oxide | 0.9.1 | MIT OR Zlib OR Apache-2.0 |
| simd-adler32 | 0.3.10 | MIT |
| typenum | 1.20.1 | MIT OR Apache-2.0 |
| version_check | 0.9.5 | MIT/Apache-2.0 |

Selected MIT/Apache-2.0/0BSD terms are GPL-3.0-or-later compatible. WASI-specific
getrandom lockfile packages are not compiled for Linux/iOS. The importer uses
tar entry parsing into the private VFS, never tar host extraction.

Downloaded Alpine and tree artifacts are test/guest distribution data, not
linked runtime libraries. Alpine contains packages with independent licenses;
retain their notices and satisfy relevant source obligations when redistributing
a rootfs. tree 2.2.1 declares GPL-2.0-or-later. OpenSSL in the guest is Alpine's
unmodified separately distributed package. A Trames project license does not
replace the guest distribution's package licenses.
