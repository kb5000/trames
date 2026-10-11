# ADR-006 — Shared userspace IP fabric with a native socket gateway

Status: accepted and implemented after M3, 10 October 2026.

The owner requested independent internal IPs, then authorized implementation
following M3. Each environment needs private loopback and service ports, and
ordinary Internet sockets must work on developer-signed iPadOS without TUN/VPN
or hardware virtualization.

Use pinned smoltcp 0.12.0 with the Medium::Ip adapter and explicit protocol
features. All internal and external TCP/UDP socket flows traverse the packet
stack. Share environments only through a trusted Fabric object. Gateway/DNS
addresses are 10.0.0.1 and fd00::1; environment IDs are 2–254. The gateway bridges
TCP streams and reassembled UDP datagrams to supported nonblocking native
sockets. A transparent gateway preserves logical remote addresses and does not
terminate TLS. The existing native backend remains available through ADR-005.

Implement missing IPv6 fragmentation in the adapter and bounded reassembly for
both IP versions, with overlap rejection and expiry. Bound namespace count,
socket/backlog reservations, frames, native flows and system DNS work. Keep
namespace generations and weak ownership to prevent a reused ID from receiving
an old session's work. Poll native write readiness only when needed.

This provides packet-level isolation and transport behavior without adding a
host virtual interface or route. Ethernet, ARP/DHCP, multicast, host inbound
forwarding, AF_PACKET and full Linux socket controls remain
outside this delivery. HTTPS/apk, two-family transports and failure/lifecycle
behavior have executable validation; signed iPad builds/device checks belong
to M4.

The 0.2.0 follow-up implements guest SOCK_RAW ICMP through the userspace stack,
read-only NETLINK_ROUTE and runtime proc/sys interface views. External echo uses
unprivileged native ICMP datagram sockets rather than privileged host raw sockets;
actual OS errors remain visible. No TUN/VPN entitlement or host route change is
added. See [Linux compatibility](../architecture/linux-compat.md).

Details: [architecture](../architecture/virtual-network.md),
[validation](../validation/virtual-network.md),
[dependency licenses](../security/dependencies.md).
