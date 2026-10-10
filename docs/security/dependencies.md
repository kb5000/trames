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
