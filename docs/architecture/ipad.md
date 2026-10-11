# iPad embedding

The SwiftUI frontend owns UIKit terminal views, native SSH sessions, Keychain,
Files authorization and app lifecycle. Rust owns the interpreted local Linux
session and its private disk store. No instruction is translated into native
executable code.

The UIKit terminal viewport has one scrolling axis: its content width follows
the visible width, and horizontal offsets/bounds origins remain zero. Vertical
scrollback stays with SwiftTerm. Rejected horizontal adjustments invalidate the
visible drawing region so UIKit cannot retain a shifted backing store. The
terminal grid still resizes normally on rotation and keyboard layout changes.

The ABI is version 1: integer opaque handles, bounded byte buffers, UTF-8 JSON
configuration/status and explicit status codes. Only crates/trames-ffi/src/lib.rs
permits unsafe code for copying caller-owned buffers. No pointers are retained;
all other runtime crates and the worker retain the safe-Rust policy. Read the
C header for buffer/ownership contracts. Panics are contained at the ABI/worker
boundary. Invalid native pointers are caller bugs; the ABI is not a memory
isolation boundary.

Every local worker constructs its Rc-based runtime/VFS/network on its own
thread. Native threads exchange bounded queues, never Rust layouts. There are
at most eight workers, including workers finishing cancellation. Each input
and output queue is 64 KiB. Input returns partial acceptance for backpressure;
output polling drains only bytes copied successfully. Resize is coalesced.
Cancellation is checked between scheduler rounds (4096 instructions/process),
and on blocked-IO iterations; pending children and sockets drop with the worker.
A full PTY queue makes the guest wait. Pause retains the session while leaving
the main thread free.

An embedding control callback runs at scheduler boundaries with the root VFS.
It handles host-authorized file copy commands without racing guest writes or
crossing Rc objects between threads. Initial installation occurs in an absent
staging store, validates the pinned archive, releases Rust owners/locks, and
only then promotes the directory. Failed installations are removed and never
become the active store. Guest stack mapping is now 1 MiB because Alpine's line
editor probes a >64 KiB frame; initial argv/envp remain capped at 64 KiB and the
64 MiB process/256 MiB aggregate memory quotas still apply.

Native SSH is separate from the guest's sockets. SwiftNIO SSH implements SSH
crypto/auth/channel state. The PTY has at most 64 queued chunks of at most 64 KiB;
overflow closes the channel with a visible error. The SFTP v3 copy subset accepts
at most a 64 KiB frame, four queued responses and one outstanding request per
transfer. It handles fragmentation, request-ID checks, EOF/status, exclusive
creation and 20-second timeouts. Cancelling a file copy closes its SFTP channel and leaves the PTY open.
If cancellation or timeout happens before a child channel is created, the
containing SSH connection is closed to release the pending operation. Citadel is used for Ed25519 OpenSSH key parsing, not its
unbounded convenience terminal streams or internal object layouts.

SSH host identity is keyed by normalized hostname and port, with exact public
key comparison and SHA256 fingerprints. Unknown hosts require an explicit UI
trust action. Changed keys are rejected and cannot overwrite a pin through the
first-use action. The native pin file lives in Application Support; credentials
are session-only unless the user selects Keychain persistence. The current
supported ~/.ssh/config subset preserves first-obtained values, wildcard and
negated Host matching. Match is rejected and other unsupported directives need
an explicit warning acknowledgement. Proxy/agent/command directives never run.

Provider access is held only around a coordinated bounded copy to private
staging. No provider bookmark or guest-accessible native path is stored. An
export is staged then copied by UIDocumentPicker. Cloud provider failures and
limited free capacity remain explicit errors. File transfers use single-use cancellation tickets, checked before dispatch
and between 64 KiB copies. Closing a local session also cancels its copies.
Configuration/key exports have a 64 KiB preflight size limit. This establishes
a small embedding ABI; the permission-brokered versioned asynchronous Bridge RPC
and accelerator registry remain M5 work.
