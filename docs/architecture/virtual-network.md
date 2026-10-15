# Userspace virtual IP fabric

> The default CLI/iPad session now uses [direct native networking](direct-network.md).
> This packet fabric remains explicitly selectable with `--virtual-network` and is
> a building block for future guest network namespaces; it is not CLONE_NEWNET.

Delivered after M3 on 10 October 2026. Each environment has an IP stack and
independent service ports. TCP/UDP external traffic also traverses packets and
the stack; a trusted gateway bridges it to supported native nonblocking sockets.
There is no host NIC, TUN, route mutation, VPN entitlement or native raw socket.

~~~mermaid
flowchart LR
    A["Environment 2: 10.0.0.2 / fd00::2"] --> P["Bounded IP packet fabric"]
    B["Environment 3: 10.0.0.3 / fd00::3"] --> P
    P --> A
    P --> B
    P --> G["Gateway: 10.0.0.1 / fd00::1"]
    G --> D["Internal DNS / bounded system resolver"]
    G --> N["Nonblocking native TCP / UDP"]
    N --> H["Host network policy and OS"]
~~~

## Addresses and ownership

| Endpoint | Addresses |
| --- | --- |
| Gateway and DNS | 10.0.0.1, fd00::1; DNS UDP/TCP port 53 |
| Environment ID 2–254 | 10.0.0.ID, fd00::ID (IPv6 ID is the numeric value) |
| Private loopback in each environment | 127.0.0.1, ::1 |
| Logical interfaces | lo (index 1), eth0 (index 2), MTU 1500 |

One Fabric supports at most 16 simultaneous environments. Distinct fabrics are
isolated, even if their IDs coincide. Environments share a fabric only when the
trusted embedder explicitly gives them that same object. Binding another
endpoint's IP returns EADDRNOTAVAIL. Private ports including 53/80 are allowed;
no native listener is opened by a virtual bind. Duplicate live bindings in the
same namespace remain rejected; SO_REUSEADDR records the request without enabling
UDP port sharing or Linux's complete reuse/TIME_WAIT semantics.

Off denies sockets and ingress. Loopback allows the environment's loopback and
own assigned addresses and denies peers/gateway. Outbound and Full allow virtual
peers and ordinary unicast gateway egress; both can listen on their own/wildcard
virtual addresses, without exposing a native listener. There is no automatic
host-to-guest forwarding. IPv6-only defaults true; setting it false before bind
supports IPv4-mapped addresses and dual-stack wildcard listeners.

The library creates a Fabric with a fallible entropy check, then
Network::virtual_network(&fabric, id, mode) or
Vfs::configure_virtual_network(&fabric, id, mode). Fork/dup share the namespace,
sockets and open descriptions. A monotonic internal generation key protects
teardown and late work when an environment ID is reused. Socket metadata uses
weak references; it does not keep a dead session alive. Network::finish and
Vfs::finish_network end the namespace explicitly. Session-root exit invokes this
and cancels its native flows. Retaining a faulted Runtime requires the embedder
to end or drop its session, as with the native backend.

## Packets, transport and limits

smoltcp 0.12.0, pinned with default features disabled, supplies IP/TCP/UDP/ICMP
processing: checksums, TCP sequencing/windowing/retransmission, connection
handshake, FIN/RST and UDP datagram boundaries. Only its Medium::Ip device is
enabled. Raw host and TUN devices, Ethernet, DHCP and multicast are disabled.
Initial TCP sequence seeds and ephemeral port selection use host entropy.

The adapter fragments outgoing IPv6 packets because this smoltcp version lacks
that implementation. IPv4 outgoing fragmentation uses smoltcp. Both versions
use adapter reassembly: out-of-order fragments are accepted, overlapping sets
are poisoned until expiry, invalid offsets/lengths are rejected, and two
incomplete datagrams per node expire after 60 seconds. Payload bounds are
65,507 bytes for IPv4 UDP and 65,527 for IPv6 UDP. No IPv6 jumbograms or arbitrary
extension-header chains are supplied by the socket API.

Each port has at most 96 incoming and 96 outgoing frames, each at most 1500
bytes. Reassembled packets are bounded by the IP length limit. The stack permits
256 aggregate handles/reservations per fabric, including gateway sockets,
backlog entries and pending native connections; 64 guest socket objects per
environment; and 64 native TCP/UDP/ICMP flows per fabric. Listener backlog is capped
at eight per addressed IP. Wildcard listeners cover assigned and loopback
addresses, plus IPv4 when explicitly dual stack, so a backlog consumes several
handles. Quota checks precede allocations/accept side effects.

TCP/UDP/ICMP socket buffers are fixed at 64 KiB each direction. UDP/ICMP also have
16 metadata slots. Guest SNDBUF/RCVBUF requests within the existing 256 KiB
request cap are accepted but getters report the fixed capacity. Full Linux
buffer autotuning is not claimed. Packet queues and two 65,535-byte reassembly
buffers with bounded coverage metadata are extra resources, as are smoltcp's
own protocol structures. These bounds constrain requests rather than promise
an exact RSS limit. Tick work is limited to 32 fabric rounds per scheduler slice.

The scheduler polls smoltcp deadlines and native interests, bounded by the
existing 20 ms wait. Native POLLOUT is requested only for a pending connect or
queued writes; full receive buffers stop native read interest. Closing a socket
can retain its TCP handle for up to 30 seconds to drain queued bytes/FIN; explicit
namespace finish resets connections and releases native flows. TCP handshake
timeout is 60 seconds; established internal connections have no arbitrary idle
timeout. Gateway TCP flows expire after 120 seconds without bridged IO (30
seconds after their last guest owner disappears); UDP flows expire after 60
seconds. These are deliberate first-version resource policies, configurable
platform policy remains future work. Unsupported Linux timeout options still
return EOPNOTSUPP.

## Native gateway and DNS

A new external TCP SYN is held until the native nonblocking connect succeeds.
The gateway then creates a transparent stack endpoint with the logical remote
IP/port. Native failures produce a checked RST and the translated guest errno,
so EINPROGRESS/SO_ERROR/blocking connect remain meaningful. Both directions
respect short transfers and bounded capacity. Native EOF sends FIN; guest FIN
shuts down native writes after queued guest data is delivered. Valid stack resets
are translated after smoltcp checks their sequence; stray out-of-window resets
cannot turn normal EOF into ECONNRESET.

UDP is reassembled before native forwarding. A connected native flow per
namespace/source/destination tuple preserves reply ownership. Replies re-enter
the gateway stack and fragment when needed. A bounded pending datagram can be
retained on EAGAIN; UDP can otherwise drop under congestion. The fabric reserves
10.0.0.0/24 and fd00::/64, including absent IDs, so absent virtual peers never
fall through to similarly addressed native services.

DNS implements bounded A/AAAA queries over UDP and TCP, including length-framed
TCP and truncated UDP replies. env2.trames, env3.trames etc resolve registered
environments; gateway.trames resolves the gateway; localhost resolves private
loopback; other .trames names return NXDOMAIN. Other names use supported host
system resolution on at most two worker threads, with eight queued jobs and
eight pending guest requests. Results contain at most eight addresses and a
30-second TTL. Unsupported query types/compression return an explicit error.
Timed-out/reused TCP DNS sessions check request tokens before delivering a late
result. A running host getaddrinfo cannot be forcibly canceled; bounded workers
can finish it without delivering to a ended session. This is ordinary system
DNS, with no encrypted-DNS or full authoritative DNS claim.

ICMP echo operates inside the fabric and at the gateway. The guest API supports
SOCK_DGRAM/IPPROTO_ICMP or ICMPV6 and bounded guest SOCK_RAW ICMP packets, including
the raw ABI used by Alpine BusyBox ping. Payloads are 8–1400 bytes. External echo
uses native unprivileged ICMP datagram sockets, never native raw sockets. At most
32 echo flows share the 64-flow limit and expire after 60 idle seconds. Replies
retain guest identifiers and logical remote addresses; unavailable host permission
or routing returns actual errors. Non-echo external raw ICMP and packet sockets
remain unsupported. iPad ICMP behavior still requires physical acceptance.
Read-only SIOCGIFCONF/NAME/INDEX/ADDR/NETMASK/MTU/FLAGS and NETLINK_ROUTE link/address/
route dumps report guest interfaces; proc/sys views expose runtime statistics.
Network configuration changes, multicast subscriptions and the full Linux
interface ABI remain unsupported. See [Linux compatibility](linux-compat.md).

HTTP/TLS stays end-to-end in the guest. The gateway neither terminates TLS nor
rewrites certificates or application requests. It opens no route unavailable to
the host and inherits no proxy credentials. Host routing, app sandbox and local
network consent still apply. Native SSH is the separate M4 transport.

## CLI

~~~sh
# A previously initialized Alpine store; enter the private network as env2.
target/release/trames-run --storage target/alpine --virtual-network 2 --guest --max-instructions 100000000 /bin/sh
~~~

The CLI creates a single environment per invocation. The library shared-Fabric
API provides multiple simultaneous network namespaces; interoperation is tested
at the socket layer. Concurrent UI/session scheduling across separate interpreter
Runtime instances belongs to the later frontend work. If /etc/resolv.conf is absent,
virtual mode creates it with nameserver 10.0.0.1; existing explicit configuration
is preserved. The default CLI backend remains the existing native backend.

Trusted --host-map VIRTUAL_IP=HOST_IP (at most 16 entries per fabric) explicitly
maps a logical external IP to a native service for integration tests or an
embedder's authorized forwarding. It is unavailable to guest syscalls, cannot
map a reserved/private/loopback virtual IP, and is the only way these fixtures
reach native loopback. No implicit mapping exposes host localhost.

See [validation](../validation/virtual-network.md) and
[ADR-006](../decisions/ADR-006-userspace-network.md). This is a userspace IP stack
with the stated socket subset, not a full Linux kernel networking subsystem or
an iPad device validation.
