# M3 network foundation validation

Date: 10 October 2026. This validates the native socket stage independently of
dynamic loading/Alpine/apk. Completed later stages are recorded in
[M3](m3.md) and [virtual network](virtual-network.md); the counts below are the
historical native-foundation snapshot.

## Reproduction

Use Rust 1.90.0, Python 3, the pinned musl/BusyBox build, AArch64 GCC and QEMU.
Install the Rust aarch64-apple-ios standard-library component for the compile
check. It needs no Apple SDK link for cargo check.

~~~sh
cargo fmt --all --check
cargo clippy --workspace --all-targets --locked -- -D warnings
cargo build --workspace --locked
cargo test --workspace --locked
rustup target add aarch64-apple-ios --toolchain 1.90.0
cargo check -p trames-core --target aarch64-apple-ios --locked
bash scripts/build-busybox.sh
bash scripts/build-network-fixtures.sh
python3 tests/integration/network.py
~~~

The harness uses native loopback IPv4 and IPv6 peers, including a deterministic
DNS server on 127.0.0.1:53. That host test peer needs permission to bind port 53.
On a Linux machine without that permission, run the harness with sudo and retain
the tool PATH, as CI does. No external Internet endpoint or host DNS configuration
is needed. QEMU -L points the musl oracle at an independently prepared resolver
configuration. Trames imports its own guest config into VFS. Guest low-port bind
restrictions continue to apply even if the host harness is privileged.

## Evidence

The fixture is an actual static AArch64 C program, compiled against unmodified
musl 1.2.5 with vectorization disabled as in the existing reference fixtures.
The runtime executes the same bytes as QEMU; it does not select behavior by file
name. Independent native peers verify payloads and socket closure.

| Case | Checks | QEMU comparisons |
| --- | --- | --- |
| IPv4/IPv6 UDP | connected/unconnected sends, binary two-iovec sendmsg, PEEK, TRUNC, zero datagrams, recvmsg peer address, dup/CLOEXEC/FIONBIO | 4 |
| IPv4/IPv6 TCP | nonblocking connect, SO_ERROR, TCP_NODELAY, 256 KiB patterned payload, partial read/write loops, shutdown write, EOF | 4 |
| IPv4/IPv6 refused connect | blocking/nonblocking paths, consumable SO_ERROR, subsequent receive/send state | 8 |
| IPv4/IPv6 virtual fork server | child blocking connect, parent blocking accept4, CLOEXEC/NONBLOCK, read/wait/EOF | 4 |
| IPv4/IPv6 native host client | real host connects to guest listener, delayed input, binary echo and native close | 4 |
| Signals | blocked UDP receive EINTR and SA_RESTART, child signal and later datagram | 4 |
| SIGPIPE | MSG_NOSIGNAL returns EPIPE; ordinary write terminates guest with SIGPIPE while runtime survives | 2 |
| Socket ABI | missing destination/peer, short sockaddr, bad input pointer, unknown option, excessive iovec count, truncated name/option outputs | 2 |
| Real musl DNS | A/AAAA over UDP; UDP truncation followed by TCP, fragmented TCP length prefix and reply | 4 |
| Total, both memory and disk backends | stdout/stderr/exit status plus peer-side observations | 36 |

Additional runtime-specific results: eight network capability outcomes, four
receive preflight outcomes and four session teardown outcomes. The preflight
case places an invalid pointer in a later iovec and verifies that the same queued
datagram remains intact. It is not compared to QEMU, which can return a partial
copy for that invalid-pointer case. Root exit and a root CPU fault release a TCP
connection held by a sleeping virtual child; the native peer observes EOF.

Six added unit tests cover IPv4/IPv6 address byte order, short/unknown structures,
capability rules including IPv4-mapped loopback, quota sharing and last-owner
lifetime across fork/dup/CLOEXEC, invalid accept output preserving a queued
connection, and invalid SO_ERROR output preserving a pending connection error.
Workspace total: 83 unit tests.

The iOS target compile check includes trames-core and its ELF/memory/CPU/Linux/
VFS/network dependencies. It exposed unused nix socket-feature references to
Apple kernel-control constants; the final backend enables only nix's poll
feature and uses socket2/std networking. No private Apple kernel-control/TUN
interface is used. This is compile validation, not an SDK link or iPad device test.

Existing BusyBox, full M2 process/libm, terminal, and storage integration checks
pass, including both memory and disk M2/PTY runs. Hosted CI also runs the existing
CPU/FP/memory differential suites. Including the network cases, the CI program/
scenario total is 490 QEMU comparisons, plus 6,860 floating cases, 159 structure
variants, 48 paired-integer SIMD snapshots and 18 terminal outcomes.

## Limits

These results establish only the documented socket subset. They do not validate
public Internet routing, LAN firewall/consent behavior, guest HTTPS certificates,
TLS libraries, apk, native SSH, all Linux options, socket timeouts, Unix sockets,
threads, epoll or an iPad app. Resolver config, certificates and package signature
verification belong to the upcoming distribution/platform stage.

See [network architecture](../architecture/network.md) and
[ADR-005](../decisions/ADR-005-nonblocking-sockets.md).
