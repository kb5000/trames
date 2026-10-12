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
The CPU, memory, core and other value-level Rust crates forbid unsafe code.
The isolated C caller-buffer boundary and static native compute helpers permit
reviewed unsafe operations; compute accepts values, never guest memory pointers.
GNU licensing does not automatically
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

## Userspace packet stack

smoltcp 0.12.0 is pinned with default features disabled. Only std, Medium::Ip,
IPv4/IPv6, TCP/UDP/ICMP, bounded fragmentation/storage and four logical IP-address
slots are enabled. Native raw-socket and TUN device features are not enabled.
Reviewed manifests for its compiled Linux/iOS closure:

| Crate | Version | Declared license |
| --- | --- | --- |
| smoltcp | 0.12.0 | 0BSD |
| managed | 0.8.0 | 0BSD |
| heapless | 0.8.0 | MIT OR Apache-2.0 |
| hash32 | 0.3.1 | MIT OR Apache-2.0 |
| byteorder | 1.5.0 | Unlicense OR MIT |
| stable_deref_trait | 1.2.1 | MIT OR Apache-2.0 |
| bitflags | 1.3.2 | MIT/Apache-2.0 |

The selected 0BSD/MIT/Apache-2.0 terms are compatible with GPL-3.0-or-later.
Preserve upstream source/license notices in redistribution. Existing cfg-if and
getrandom licenses are recorded above. Optional defmt/proc-macro packages in
Cargo.lock are not in the compiled Linux/iOS feature closure. This review does
not substitute for an upstream security audit.

## iPad frontend and embedding ABI

The native Swift closure is pinned in apps/ios/TramesKit/Package.resolved.

| Package | Version | License |
| --- | --- | --- |
| bigint | 5.7.0 | MIT |
| citadel | 0.12.1 | MIT |
| swift-asn1 | 1.6.0 | Apache-2.0 (with upstream notices/exceptions) |
| swift-atomics | 1.3.1 | Apache-2.0 (with upstream notices/exceptions) |
| swift-collections | 1.6.0 | Apache-2.0 (with upstream notices/exceptions) |
| swift-crypto | 3.12.3 | Apache-2.0 (with upstream notices/exceptions) |
| swift-log | 1.11.0 | Apache-2.0 (with upstream notices/exceptions) |
| swift-nio | 2.81.0 | Apache-2.0 (with upstream notices/exceptions) |
| swift-nio-ssh | 0.3.4 | Apache-2.0 (with upstream notices/exceptions) |
| swift-system | 1.6.6 | Apache-2.0 (with upstream notices/exceptions) |
| SwiftTerm | 1.20.0 | MIT |

Upstream license/notice files are included in the app resources under Notices.
Citadel is used for Ed25519 OpenSSH key parsing (including its BSD bcrypt/blowfish
C code); its terminal/SFTP convenience streams are not used. SwiftNIO SSH uses
the pinned Wellz26 fork. Swift crypto includes BoringSSL with the notices and
exceptions retained by its upstream license. The selected MIT/BSD/Apache-2.0
terms are compatible with GPL-3.0-or-later. Apple system frameworks are platform
system libraries. No signing credentials are repository dependencies.

The embedding JSON boundary adds serde/serde_core/serde_derive 1.0.229,
serde_json 1.0.151, itoa 1.0.18 (MIT OR Apache-2.0), memchr 2.8.3
(selected MIT) and zmij 1.0.23 (MIT). Existing proc-macro dependencies retain
their previously recorded terms. The isolated caller-buffer boundary permits
unsafe code; C buffer validity is a native caller contract.

## Historical M5 build tooling

The removed optional C++ interpreter used build-only cc 1.7.0, find-msvc-tools 0.1.14 and
shlex 2.0.1, each MIT OR Apache-2.0 (selected MIT). They compile project-owned
GPL-3.0-or-later source and are not linked runtime plugins. Bridge uses the already
reviewed serde/serde_json/sha2 dependency closure. No dynamic native dependency
or downloaded native plugin is introduced.

## Static native compute

`trames-compute` is project-owned GPL-3.0-or-later Rust with static ARM64 inline
assembly, plus an x86_64 development-host implementation excluded from ARM64
builds. It adds no runtime library dependency. Optional host instructions use
Rust feature detection. Architectural entropy reads reuse the reviewed
getrandom dependency; they do not synthesize random data with a PRNG.
