# ADR-005 — Native nonblocking sockets behind the guest Linux ABI

Status: accepted for the M3 network foundation, 10 October 2026.

Guest programs need ordinary TCP/UDP sockets so their existing musl resolver,
HTTP clients and TLS libraries can run. Native SSH remains a separate terminal
transport. The Linux reference runtime and future iPad frontend must not block
the interpreter on a connection or read.

Use trames-net for bounded opaque native sockets and explicit host errno/flag
translation. trames-linux validates AArch64 guest structures and pointers;
trames-vfs owns shared open descriptions; trames-core suspends and wakes virtual
processes. Host descriptors are never guest descriptors. Native sockets always
remain nonblocking, independent of the shared guest O_NONBLOCK flag.

Use the safe interfaces of socket2 0.6.1 and nix 0.31.3. Socket2/std provide RAII sockets,
connect/accept/options/receives; nix provides borrowed-descriptor poll. Trames
uses these without unsafe code in this project. Their selected licenses and locked transitive dependencies are recorded
in [dependencies.md](../security/dependencies.md). Dependencies contain their
own platform FFI; Trames initially prohibited unsafe code in its value-level sources,
not a claim that the complete dependency graph contains no unsafe code.

Blocking host IO would stall all virtual processes. One worker thread per socket
would require additional queues, cancellation ownership and partial-transfer
accounting. A nonblocking poll backend needs neither. Per-call readiness queries
use zero timeout; each native namespace has at most 64 socket handles. The later shared-fabric
scheduler permits a bounded aggregate wait of 1024 native/gateway handles.
Level-triggered polling also avoids edge-triggered rearm hazards in this first
backend. An epoll/kqueue reactor can replace this when profiling warrants it.

The app selects network capability before running a session. The library starts
with networking off. The CLI defaults to outbound traffic, ephemeral UDP clients
and loopback listeners; non-loopback TCP/selected-port UDP listeners require
--network full. This is host networking, not a private virtual network namespace.
Loopback denotes the host application's loopback.

DNS wire data and TLS are handled by the actual guest libraries. The runtime does
not treat a socket as an HTTP request, terminate TLS, skip certificate checks, or
inherit host proxy credentials. DNS configuration and root certificates belong
to distribution/platform setup, not CPU interpretation.

The initial implementation supports the documented IPv4/IPv6 TCP/UDP subset.
Unsupported control messages, raw sockets, Unix-domain sockets, interface scopes,
timeouts and message flags fail explicitly. Linux execution is validated; an
iOS target compile check does not replace signed-build or device validation.
See [network.md](../architecture/network.md) for behavior and remaining work.

The post-M3 userspace fabric and native gateway are recorded in [ADR-006](ADR-006-userspace-network.md).

Owner-directed update, 2026-10-13: audited unsafe boundaries are permitted;
explicit unsafe operations and documented ownership/aliasing/permissions are
required. The socket capability restrictions of this decision remain in force.
See [current boundary policy](../security/dependencies.md).
