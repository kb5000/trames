# iPad frontend

The SwiftUI app opens the private Alpine 3.22.6 ash shell on launch. Local Linux
and native SSH use SwiftTerm with Unicode, VT escape sequences, true color,
selection and hardware/software keyboard support. Sessions have tabs, an
editable extra-key row, and explicit close controls.

SSH uses SwiftNIO SSH 0.3.4 (the Wellz26 fork), strict public-key pinning and an
explicit first-use fingerprint prompt. Password and Ed25519 OpenSSH keys are
supported; encrypted Ed25519 parsing comes from Citadel 0.12.1. Optional saved
credentials use WhenUnlockedThisDeviceOnly Keychain items. Unsupported config
directives are listed before connection; Match is rejected. Load the canonical
/home/user/.ssh/config from the local environment. IdentityFile paths resolve
inside that environment; no guest path grants native file access.

SFTP v3 supports file upload/download with exclusive creation, 32 KiB chunks,
64 KiB frame limits, bounded queues and 20-second handshake/request deadlines,
including writes stalled by SSH flow control. It shares the
native SSH connection but uses its own session channel. Directory browsing,
forwarding, agents, jump hosts and arbitrary SSH config execution are future work.

Files import uses a security-scoped, coordinated stream copied to private
staging. Export uses the system document exporter with a private staging copy.
There is no live POSIX mount of a provider URL. Transfers never silently
truncate an existing file. A failed remote upload may leave a partial file if
its cleanup request cannot reach the server.

The default logical budget is 16 GiB, constrained by native free capacity.
Settings can select the app volume's total capacity on the next local launch.
One local environment owns its exclusive persistent-store lock. Other tabs are
native SSH sessions. The multi-environment virtual Fabric remains a Rust host
API; sharing it among iPad sessions requires a future single-thread scheduler.

Entering the background pauses the local interpreter and closes SSH sessions.
Returning to the foreground resumes the local session. Initial download/install
and provider operations are finite background work, not an iOS background daemon.

## Build

Use macOS 15, Xcode 16 or newer with the iOS SDK, Swift 6 and Rust 1.90.0:

    brew install xcodegen
    bash scripts/build-ios.sh simulator
    bash scripts/build-ios.sh device

The scripts build the Rust static library, generate Trames.xcodeproj from
project.yml, and link unsigned arm64 simulator/device apps. Swift package
versions and transitive pins are recorded in TramesKit/Package.resolved.

For ordinary developer signing, open the generated Trames.xcodeproj, select your
own Development Team and a unique bundle identifier, select a connected iPad,
and build/run. No private entitlement, JIT or virtualization capability is used.
Build the matching Rust target first with the script. Build settings select
its library by SDK; no developer team or credential is committed to the repo.

macOS CI tests the native SSH PTY, window changes and a 1 MiB SFTP roundtrip
against an actual local OpenSSH server, then links both unsigned Apple targets.
Unsigned builds are not developer-signed or device-validated builds. Complete
the device checklist in docs/validation/m4.md before marking M4 accepted.

## If you only have an iPad

GitHub Actions supplies the Mac for Apple builds and simulator testing; a local
Mac is not needed for these checks. Its artifact contains unsigned apps, the
generated Xcode project and test reports. An unsigned `.app` cannot be installed
directly on a physical iPad.

Device installation still needs your Apple signing identity and provisioning
profile. A cloud Mac can perform that signing once configured with your own
developer assets. TestFlight additionally requires Apple Developer Program
membership and App Store Connect setup. This repository has no signing assets,
configured distribution or completed physical device acceptance yet.
