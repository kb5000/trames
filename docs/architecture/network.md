# M3 network foundation

This stage implements real native IPv4/IPv6 TCP and UDP for interpreted AArch64
Linux programs. Dynamic ELF, an Alpine rootfs, apk and native iPad networking
validation remain subsequent work; this is not completion of M3.

## Boundaries

~~~mermaid
flowchart LR
    G["AArch64 program: musl / HTTP / TLS"] --> L["trames-linux: checked Linux ABI"]
    L --> V["VFS: guest fd and shared open description"]
    V --> N["trames-net: policy, quota, opaque socket"]
    N --> H["socket2 / nix: nonblocking host TCP/UDP"]
    C["core: process waits and signals"] --> V
    C --> N
    H --> O["Operating system network"]
~~~

No guest pointers, raw sockaddr structures, integer descriptors, message flags
or errno values are forwarded blindly. Sockaddrs are decoded/encoded explicitly:
guest family and IPv6 scope are little endian, ports and IPv6 flow info are
network endian. IPv4 uses 16 bytes and IPv6 28 bytes. Native errno symbols map to
Linux AArch64 numbers, including on Darwin.

Each session has a shared 64-socket quota, including accepted sockets. FD tables
remain bounded at 256 per process. Dup/fork reference the same open description
and socket; status flags are shared, CLOEXEC is per descriptor. Exec closes
CLOEXEC entries. Last-owner drop closes the native socket without a worker or
queued callback referring to a recycled handle. Socket objects are not stored in
the disk manifest, so restarting a session never restores an old connection.

## IO and process scheduling

Native descriptors are always nonblocking. The guest O_NONBLOCK flag and
MSG_DONTWAIT decide whether EAGAIN is returned or the virtual process suspends.
A blocked read/accept/receive/write/send retries only after an operation returned
no transferred data. A successful short transfer returns immediately; it is never
replayed as a whole operation.

TCP connect starts once. A nonblocking guest gets EINPROGRESS, observes readiness
and obtains the translated pending result through SO_ERROR. A blocking guest
suspends, then resumes the existing connection attempt. Repeated guest connect
while pending returns EALREADY. SO_ERROR consumes a pending error once; invalid
output buffers cannot clear it. After a failed connection's error is consumed,
nonblocking-connect reads return EOF, blocking-connect reads ENOTCONN, and sends EPIPE. Reuse of a failed TCP socket is not supported;
create a new socket. No interpreter-managed connect timeout is claimed.

Readiness is level triggered with native poll, checked at zero timeout. When
all processes wait, the scheduler deduplicates native handles and waits on their
requested interests, for at most 20 ms and no later than a guest timer deadline.
Terminal/plain stdin polling bounds that wait to 1 ms. Socket events wake poll
directly. Guest sleeps and signal-producing children still get execution slices.
This avoids one blocking host thread per socket and unconditional 1 ms socket
polling. The future app can replace this wait mechanism with its event loop.

Guest ppoll/pselect and blocking socket calls are recognized as external waits;
the runtime does not incorrectly report deadlock while a native peer can respond.
Catchable signals interrupt blocked IO with EINTR. SA_RESTART replays
read/write/send/recv/accept only when no bytes or connection were consumed.
An interrupted connect remains in progress, can be polled and closed, and is
not blindly reissued. Socket timeout options are rejected rather than pretending
a deadline was installed. Guest programs can use nonblocking IO and ppoll timers.
The instruction budget alone is not a wall-clock deadline.

MSG_NOSIGNAL suppresses guest SIGPIPE. All native sends suppress host SIGPIPE
(MSG_NOSIGNAL on Linux, SO_NOSIGPIPE on Apple). Write/writev and sends without the
guest flag queue guest SIGPIPE on EPIPE; ordinary guest signal handling applies.
TCP shutdown, EOF, refused connections and half-close use the stated Linux ABI.

Session-root exit drops remaining virtual children and their connections. On
CLI faults, dropping the runtime does the same. An embedder retaining a Runtime
after a fault also retains its root descriptors for inspection; it must close or
drop them when ending that session. There is no retained native daemon.

## Guest buffers and datagrams

Payload pointers, msghdr, all iovecs, optional peer-address output and length
fields are validated before native side effects. If any destination is invalid,
Trames returns EFAULT without accepting a connection or consuming a packet.
This deliberately preflights the entire call: Linux/QEMU can instead expose a
partial copy or consume data on some invalid-pointer paths. That stronger failure
atomicity is tested separately, not asserted as a QEMU equivalence.

TCP transfers copy at most 1 MiB per call. sendto/sendmsg reject larger payloads
with EMSGSIZE rather than silently altering a datagram. An IPv4 UDP payload may
be at most 65,507 bytes; IPv6 at most 65,527 bytes. UDP receives use one 64 KiB
scratch buffer to preserve datagram boundaries. MSG_PEEK does not consume data;
MSG_TRUNC reports the complete length when requested, and recvmsg sets its
output MSG_TRUNC when a smaller buffer was used. Zero-length UDP receives still
consume one datagram. No unbounded interpreter-side receive queue exists.

Each socket initially requests 64 KiB native send/receive buffers; guest requests
are capped at 256 KiB each. Actual OS-reported sizes can differ (Linux doubles
requests) and are returned by getsockopt. The native OS also owns protocol metadata
and backlog resources; this is a bounded request policy, not an exact RSS bound.
Listen backlog requests are capped at 128.

## Capability modes

The library VFS defaults to Off. The CLI selects Outbound unless specified.
Modes are configured by the embedding app, never guest syscalls. A capability
cannot be replaced while that VFS network namespace has live socket objects.

| CLI mode | Connections / UDP sends | Explicit bindings |
| --- | --- | --- |
| off | socket creation denied | none |
| loopback | loopback only, including IPv4-mapped IPv6 loopback | loopback only |
| outbound | ordinary unicast host destinations | loopback TCP/UDP; wildcard UDP port 0 for clients |
| full | ordinary unicast host destinations | also non-loopback/wildcard listeners |

Ports below 1024 are denied for guest bind in every mode, independent of whether
the containing host process happens to have privilege. TCP listen must follow an
explicit successful bind; it cannot implicitly open a wildcard endpoint. Full
mode uses actual native interfaces and ports, and can conflict with host services.
Loopback addresses access native loopback, including other host services; there
is no isolated guest IP stack. An outbound UDP client's wildcard ephemeral socket
can receive packets sent to that port. These modes are coarse capabilities, not
complete host-network isolation or per-domain egress policy.

All modes remain subject to the containing app's OS sandbox, routing, configured
network access, local-network consent and VPN/proxy policy. The runtime cannot
grant a route unavailable to the host. It does not tunnel around host restrictions.

## Supported ABI

| Group | Implemented subset |
| --- | --- |
| Families/type/protocol | AF_INET/AF_INET6; SOCK_STREAM TCP and SOCK_DGRAM UDP; protocol 0/6/17; NONBLOCK/CLOEXEC |
| Lifecycle | socket, bind, listen, accept/accept4, connect, getsockname/getpeername, shutdown, close |
| Payload | read/write/writev, sendto/recvfrom, sendmsg/recvmsg with bounded iovecs |
| Flags | send DONTWAIT/NOSIGNAL; receive DONTWAIT/PEEK/TRUNC (TRUNC for UDP) |
| Readiness | ppoll/pselect; errors and hangups; no epoll yet |
| Options | REUSEADDR, KEEPALIVE, SNDBUF/RCVBUF, TYPE, DOMAIN, PROTOCOL, ACCEPTCONN, ERROR, TCP_NODELAY, IPV6_V6ONLY |
| Sharing | dup/dup3/fcntl, FIONBIO, fork, exec CLOEXEC and per-session quota/lifetime |

Not yet supported: Unix-domain/socketpair IPC, raw/packet sockets, multicast and
broadcast controls, IPv6 interface scopes, ancillary/SCM_RIGHTS messages,
WAITALL/OOB/MORE, socket timeouts, accept filters, epoll, readv, sendmmsg/recvmmsg,
IP/TCP tuning options outside the table, or full Linux interface enumeration.
Nonzero ancillary sends fail EOPNOTSUPP. Receives produce no ancillary messages
and report zero control length. Unknown options fail ENOPROTOOPT; unsupported
flags fail EOPNOTSUPP. IPv6-only is initially true and can be explicitly changed
before bind, as musl does for mixed DNS servers.

## DNS, proxies, TLS and iPad

Real musl reads guest /etc/hosts and /etc/resolv.conf, sends A/AAAA DNS queries,
checks peer/transaction IDs and performs TCP fallback on truncation. Both DNS
transports are tested using a deterministic loopback DNS peer. The runtime does
not synthesize hostname-specific responses. Linux distro setup can import an
explicit resolver configuration; importing it does not import credentials.

HTTP and TLS run inside the actual guest program. Guest proxy variables/config
are explicit distribution/application data and are not inherited from host
environment secrets. A future apk stage must install a verified certificate
bundle and validate TLS and package signatures. TCP success alone is not HTTPS
or apk validation.

Ordinary BSD sockets and poll are available to the planned iPad app without JIT,
TUN, virtualization or a downloaded native plugin. The network/core code passes
an aarch64-apple-ios compile check. No Apple SDK link, signed app or device
network behavior has been validated. Apple-specific SIGPIPE and errno handling
are in the backend; AArch64 guest structures stay in the Linux ABI layer.

M4 must integrate app lifecycle cancellation/suspension and OS path changes,
scope any local-network consent, and select DNS through supported native APIs or
explicit resolver configuration. Do not assume iOS exposes a usable host
/etc/resolv.conf. A platform DNS forwarder/system-resolver adapter, if needed,
must have a specified packet subset, bounded requests and cancellation; it is not
implemented here. Native SSH host-key verification remains separate from guest
sockets. Network.framework can supply native session/path integration without
changing guest TLS behavior.

Next: PT_INTERP/file-backed private mmap, verified Alpine files and dynamic
program execution, then HTTPS/apk operations and execution of installed binaries.
