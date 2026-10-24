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
/root/.ssh/config from the local environment. IdentityFile paths resolve
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
API; sharing it among iPad sessions requires coordinated environment scheduling.
Within the local environment, guest threads now use the [multicore CPU pool](../../docs/architecture/multicore.md),
with the host-reported available CPU count capped at 32. Linux/VFS and NativeCall
remain coordinator-owned. Physical-device performance is not established.

Entering the background pauses the local interpreter and closes SSH sessions.
Returning to the foreground resumes the local session. Initial download/install
and provider operations are finite background work, not an iOS background daemon.

## Build

Use macOS 15, Xcode 16 or newer with the iOS SDK, Swift 6 and Rust nightly-2026-10-16:

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

### File Provider signing and shared storage

The app includes a File Provider extension. In Files, open the app's location,
then Alpine, to browse directories and open or export regular files. Editors
that support saving in place can modify existing regular files with UTF-8 paths.
Creating files, renaming and deletion are not implemented. Symbolic links and
special Linux nodes are visible but cannot be opened as regular files.

Edits are deferred: the extension streams an immutable saved copy into the App
Group journal, and Trames commits it before guest execution resumes on the next
foreground activation or initial launch. If staging finishes after the app has
already returned to the foreground, a Darwin notification triggers replay; an
in-flight replay coalesces later save events before releasing the paused guest.
The extension never opens a writable
VFS. Repeated saves coalesce per document. A conditional commit compares the
original inode and SHA-256; stale edits, quota failures and interrupted commits
retain their saved copy. Files exposes these copies under `待写回的编辑` (Pending
Edits) for export, and Trames reports replay failures. Document identifiers stay
stable across atomic content replacement. An idempotent commit receipt allows
recovery when the app exits after writing but before acknowledging the journal.

There is no new guest instruction/I/O hook, polling service or worker thread.
Replay is driven by foreground activation, using the existing VFS owner while
its guest is paused. Actual saves still cost streaming copies, SHA-256 checks,
temporary disk space and the VFS's existing atomic namespace checkpoints; large
edits can delay resumption. Copies use 64 KiB buffers and drain autoreleased
Foundation objects after each chunk, so large documents do not accumulate in
extension memory. Ordinary Files import/export staging uses the same chunk
lifetime discipline. This isolates the steady-state runtime; saving still costs
I/O and latency.

Large transfers remain streamed; they are not loaded into a single `Data`/`Vec`.
Disk usage is proportional to the file: alongside the original guest object,
there can be a provider cache, a pending payload and a replacement guest object.
Repeated saves briefly retain the previous payload too, and existing Linux open
handles can retain an old inode. Allow several file-sized copies of free space.
Quota/space failure during replay leaves the original guest path and pending edit
intact; staging failure preserves the editor's cache and reports a provider error.

For the explicit large-file probes, set `TRAMES_FILEPROVIDER_LARGE_MIB=256` when
running `RootfsEditStoreTests` and
`BridgeTests.testLargeFilesStagingUsesBoundedMemory`, and run
`python3 tests/integration/fileprovider-writeback.py --library <ffi-library> --store <disposable-alpine-store> --large-mib 512`.
The ABI probe copies its input store, checks a conditional replacement and a
checksum-verified export while paused, then resumes and checks guest readback.
These probes measure host peak-RSS growth, not iPad throughput. On native macOS,
the 256 MiB journal probe grew by under 1 MiB after adding per-chunk autorelease
pools (previously about 515 MiB); the 512 MiB FFI commit/export probe grew by about
2 MiB. The ordinary 256 MiB Files-import staging probe also stayed under 1 MiB
of peak-RSS growth. Run the size probes explicitly; large XCTest cases are skipped by
default.

For a developer build, keep the extension embedded, give it a bundle identifier
prefixed by the containing app's identifier, and sign both targets with the same
team. Set `TRAMES_APP_GROUP` to an App Group registered for both targets and keep
`Trames.entitlements` on both. `Trames-Info.plist` supplies the containing app's
shared-container identifier before signing; arbitrary `INFOPLIST_KEY_` settings
alone do not reliably add custom keys to Xcode's generated plist. The existing
unsigned packaging finalizer also validates matching app/extension metadata.

On the first launch with a shared container, the app moves its existing Linux
store into that container. If shared storage is unavailable, migration fails
before moving the store, or both locations already contain stores, the app keeps
using the private store. Browsing reads the persisted rootfs manifest and
materializes regular-file copies. Directory refresh notifications use the app's
existing foreground polling loop; this does not keep the Linux interpreter
running in the background. All native opens, installation and resets use the same
app-private VFS owner lock; retaining a lock inside the shared App Group while
iOS suspends the app causes a `0xdead10cc` termination. The extension never takes
that lifetime lock. Only edited-document recovery copies enter the
provider working set; the Linux rootfs is not indexed wholesale.

Provider URL identity uses file-URL path components, normalizes Darwin's system
aliases (`/private/var` and `/var`, for example), and checks the canonical filename.
Save callbacks can use a different alias from `documentStorageURL`. Do not use
`standardizedFileURL` for placeholder containment: on Apple
platforms it can remove `/private` from an existing storage directory while
retaining it on an unmaterialized child. That difference rejected the provider's
own URLs before placeholder creation. Native ARM64 XCTest regressions cover
identity before/after file creation, system aliases, outside paths and canonical
filenames.

The provider's own coordinated reads use
[`NSFileProviderManager.providerIdentifier`](https://developer.apple.com/documentation/fileprovider/nsfileprovidermanager/provideridentifier)
as their purpose identifier, so staging an edit does not request another download
over the just-saved bytes. Materialization also preserves modified cache content
before a delayed `itemChanged` callback, including a revert of a pending save.

Physical development checks on 2026-10-24 confirmed Personal Team provisioning of
the app and extension with matching App Groups, wireless installation, reopening
the retained rootfs in shared storage, Files directory browsing, and an observed
directory updating after guest file creation and content-size changes. After the
URL fix, Files Quick Look displayed the updated guest text through the provider.
An independent temporary editor then used the system document picker and
`NSFileCoordinator` to save in place. The saved copy existed in the journal while
the Linux object still contained its original bytes; returning to Trames committed
the edit, cleared the journal and showed the new text in the guest terminal.
A second save of the same document also committed successfully. A 256 MiB
coordinated save then produced a matching pending size/SHA-256, committed on
foreground activation in the same surviving process, and passed an independent
streamed checksum of the committed object plus guest `stat` size verification.
A final editor save was scheduled eight seconds later under a short system
background task. Trames returned to the foreground before that save ran, then
committed it through the completion notification without another app switch;
guest readback and the cleared journal confirmed completion. The temporary
editor was removed after validation, leaving the test document small again.
These checks use a developer build; the broader device checklist remains in
[issue #8](https://github.com/kb5000/trames/issues/8).

macOS CI tests the native SSH PTY, window changes and a 1 MiB SFTP roundtrip
against an actual local OpenSSH server, then links both unsigned Apple targets
and runs the app on an iPad simulator through installation, rotation, exit and
persistent-store reopening. Both the test exit status and xcresult summary must
pass. Validated revisions are recorded in docs/validation/m4.md.
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
configured App Store Connect distribution or completed physical device acceptance
yet. The owner confirmed Windows/Sideloadly installation and launch on a physical
iPad on 2026-10-11; the full device checklist remains open.

## Sideloadly on Windows

You can use a Windows PC and a free Apple ID instead of a paid developer
membership. The [Sideloadly documentation](https://sideloadly.io/faq) describes
signing, device trust, Developer Mode and automatic refresh. The current app
requires iPadOS 17 or newer and does not need JIT activation.

Current validated update: [0.14.0 (build 15) GitHub Release](https://github.com/kb5000/trames/releases/tag/v0.14.0).
Seven simulator tests passed and all 41 IPA files match the actual ARM64 device app.
This delivers parallel NativeCalls, 19 pure functions, 64-byte region access and sparse
checkpoint publication. Experimental native malloc/free remains disabled by default.
See the [release record, source and verification](../../docs/validation/release-0.14.md).
The [0.13.0](../../docs/validation/release-0.13.md) and
[0.12.0](../../docs/validation/release-0.12.md) releases retain their historical evidence.
Download and sign `Trames-0.14.0-build15-unsigned.ipa` directly with the same
Sideloadly Apple ID/effective bundle ID as before to retain Linux data. No data reset
or JIT activation is required. Public Release assets retain a fixed tag and paired
source; Actions artifact retention does not apply to them.

Previously validated update: [0.8.0 (build 9) unsigned IPA artifact](https://github.com/kb5000/trames/actions/runs/38236217045/artifacts/11741258008).
The local runtime now has one Rust cached interpreter with static ARM64 native
compute and embedding ABI 3. No JIT activation is required. Expanded CPU coverage,
explicit PAC/BTI/zero-tag compatibility, synchronous Linux CPU signals and the
removed C++/tail-dispatch executors are recorded in
[the coverage and delivery evidence](../../docs/validation/instruction-coverage.md).
For that historical 0.8 build, both Linux and Apple CI passed; all seven simulator tests
passed, and all 36 IPA files match the actual device app build.
Unzip it and sign `Trames-unsigned.ipa` with the same Sideloadly Apple ID/effective
bundle ID as before to retain the current Linux environment. No data reset is
needed. Physical iPad performance remains owner acceptance work.
Artifacts are retained for 14 days. The known intermittent NIOSSH dependency
issue in [#9](https://github.com/kb5000/trames/issues/9) remains open; this build's
native SSH test passed. Historical 0.7 measurements are preserved in
[the earlier record](../../docs/validation/nonjit-0.7.md).

1. Open the [GitHub Release](https://github.com/kb5000/trames/releases/tag/v0.14.0)
   and download `Trames-0.14.0-build15-unsigned.ipa` directly. It needs no extraction.
   Alternatively, download **Trames-unsigned-sideload** from a successful
   **iPad frontend** Actions run and unzip it on Windows to obtain
   `Trames-unsigned.ipa`. Use the IPA, not the artifact ZIP or simulator `.app`.
   Actions artifacts expire after 14 days; public Release assets remain available.
2. Install Sideloadly from its official website. Follow its Windows setup links
   for the web versions of iTunes and iCloud, rather than Microsoft Store builds.
3. Connect the unlocked iPad by USB, tap **Trust** on the iPad, and select it in
   Sideloadly. Drop the IPA into Sideloadly, enter your Apple ID and click **Start**.
   Complete sign-in and two-factor prompts locally; no Apple account credential
   belongs in this repository or its CI configuration.
4. If prompted, trust your developer identity under **Settings > General > VPN &
   Device Management**, and enable **Settings > Privacy & Security > Developer
   Mode**, including its restart and confirmation prompts.
5. Launch Trames with Internet access for the initial Alpine download. The
   installed private Linux environment can subsequently reopen offline.

Free signing normally lasts seven days. Sideloadly's automatic refresh requires
the Windows PC to be running and able to reach the iPad over USB or configured
Wi-Fi pairing. For updates or renewal, keep the same Apple ID and bundle ID and
install over the existing app. Deleting the app also deletes its private Linux
store; export important files before deleting it.

The free-account limit of ten App IDs per seven days concerns newly registered
identifiers, not ten versions of the same app. Keep Sideloadly's effective signed
bundle ID stable (including any custom override) to update the existing app.
The repository's bundle ID is `org.trames.Trames`. Future feature changes will be
grouped into a larger release at the owner's request; each CI build is not an
installation request.

CI packages only the arm64 iPhoneOS device app after the simulator checks pass,
retains its executable permissions and bundled resources, and includes a SHA-256
checksum. The IPA is unsigned until Sideloadly signs it with your account.
The owner confirmed Sideloadly installation and launch of 0.1.0 on 2026-10-11.
The reported horizontal shift during `apk add vim` has a viewport fix first built in 0.1.1;
verification of that fix on the owner's iPad and the remaining physical device
checklist are still pending.
The combined 0.2.0 (build 3) update includes it plus Linux runtime endpoints,
ownership, musl thread/timer support and ICMP. Local sessions now use their own
virtual fabric as environment 2. Existing upstream/default resolver configurations
migrated to virtual DNS in 0.2.0; explicit custom resolvers remained intact.
That release also converted manifest v1/v2 to v3. These historical conversion
branches are removed in 0.3.0 at the owner's request; see the reset guidance below.

To package an existing device build yourself:

    python3 scripts/package-ios.py \
      apps/ios/build/device/Build/Products/Release-iphoneos/Trames.app \
      apps/ios/build/sideload/Trames-unsigned.ipa

## Current source architecture

Device packages use Xcode Release for the Swift app and Cargo Release with thin
LTO for the Rust runtime. Simulator tests keep Xcode Debug; the embedded Rust
runtime is still Release. No signing or JIT entitlement changes are needed.

The local runtime now has one Rust interpreter and no execution-backend switch.
Native arithmetic helpers compile only the ARM64 implementation into the iPad
app. The x86_64 helper supports the development/CI host; Linux guests remain
AArch64. See [the interpreter design](../../docs/architecture/nonjit.md).
Current source is 0.9.0/build 10 with embedding ABI 3. The historical 0.7 release predates this source architecture. The expanded CPU profile and explicit
PAC/BTI/tag compatibility are recorded in
[the coverage evidence](../../docs/validation/instruction-coverage.md).

## M5 / 0.3.0 build 4

The validated combined update adds per-request Bridge consent, persistent Files folder
authorizations, an optional C++ backend and an explicit Linux environment reset.
First-install downloads wait for Apple network connectivity and automatically
retry transient connection/DNS failures up to three times. Allowing the initial
network prompt can therefore resume preparation without restarting the app.
The owner chose fresh development data; no historical v1/v2 store conversion or
old-default DNS/quota migrations remain. Settings > Reset Linux waits for the
store lock and deletes only the private Linux environment. The app
can instead be deleted and reinstalled with the same Sideloadly signing ID;
deleting the whole app also removes native settings, so retain anything needed
before doing so. Public DNS can be selected by editing the guest resolv.conf;
10.0.0.1 itself is the virtual gateway. Apple DNS/HTTPS is covered by the new
simulator regression; physical device verification remains required.
