# Virtual packet network validation

Date: 10 October 2026. M3 completed first; this records the subsequent userspace
IP fabric with its explicit [supported subset](../architecture/virtual-network.md).

## Reproduction

~~~sh
cargo fmt --all --check
cargo clippy --workspace --all-targets --locked -- -D warnings
cargo build --workspace --locked
cargo test --workspace --locked
cargo check -p trames-core -p trames-distro --target aarch64-apple-ios --locked
bash scripts/build-busybox.sh
bash scripts/build-network-fixtures.sh
bash scripts/build-virtual-network-fixtures.sh
bash scripts/prepare-alpine.sh
cargo build --release --workspace --locked
python3 tests/integration/virtual-network.py
~~~

The new harness binds only ephemeral native loopback ports. It needs no native
port-53 privilege, external DNS or host route changes. Trusted logical-IP mappings
connect the gateway to independent Python TCP/UDP/TLS peers. Guest loopback stays
private. Artifact preparation downloads the pinned Alpine rootfs and signed test
package/index with the existing policy-respecting preparation script.

## Results

| Case | QEMU comparisons |
| --- | --- |
| IPv4/IPv6 UDP binary iovecs, PEEK/TRUNC, zero datagrams and FIONBIO | 4 |
| IPv4/IPv6 TCP, 256 KiB patterned transfers, partial writes/reads and half-close EOF | 4 |
| IPv4/IPv6 blocking/nonblocking native refusal and consumed SO_ERROR | 8 |
| IPv4/IPv6 forked server on private loopback | 4 |
| EINTR and SA_RESTART with child signals and delayed virtual UDP | 4 |
| Dynamic Alpine HTTPS apk update, signed add with BusyBox trigger, installed tree | 3 |
| Total | 27 |

Both memory and disk backends are used for the 24 static guest scenarios.
QEMU uses actual musl/native loopback; Trames uses a logical external peer
through packets and the native gateway. The optional fixture peer environment
variable changes only its destination, not the operations or expected result.
For HTTPS, URL text is normalized for the distinct logical/native IPs. The test
certificate authenticates both addresses. The independent QEMU apk install omits
scripts because that host has no Trames chroot/exec supervisor; the Trames install
runs the real BusyBox trigger and then executes the installed program.

Additional checks, without an inappropriate host-interface QEMU equivalence:

- Two actual AArch64/musl runs verify SIOCGIFCONF/ADDR/MTU/INDEX, the private
  interfaces, simultaneous DNS A/AAAA for env2.trames and IPv4/IPv6 ICMP echo.
- Four guest receive-buffer preflight checks preserve the pending datagram.
- An untrusted CA is rejected by unmodified Alpine apk through the same gateway.
- Eleven Rust integration tests cover independent IP/port/loopback namespaces,
  IPv4/IPv6 TCP and UDP, connected-peer filtering, dual-stack mapped addresses,
  maximum 65,507/65,527-byte UDP datagrams, native gateway transfer/half-close,
  refusal/SO_ERROR, internal UDP/TCP DNS, the bounded system-resolver worker,
  ICMP PEEK, packet loss/corruption with successful TCP retransmission, capability
  modes, aggregate listener quota, explicit reset/teardown and environment-ID reuse.
- Two packet unit tests run IPv4 and IPv6 reordered reassembly, poisoned overlap,
  incomplete expiry and bounded queues.

Final local integration completed in 90.3 seconds alongside the native M3 regression. Workspace total: 91 unit tests and
11 Rust virtual-network integration tests. Historical program/scenario total
including this delivery is 554 QEMU comparisons, plus 6,860 floating cases,
159 structure variants, 48 pairwise integer snapshots, 72 widening snapshots,
and nine terminal outcomes per storage backend. These categories are distinct.

Linux cargo fmt/clippy, the workspace checks and iOS target compile check pass.
Hosted CI runs the entire existing CPU/M1/M2/storage/native-network/M3 suite plus
this new suite. A cargo check proves type/platform compilation only, not an Apple
SDK link, signed build, native LAN policy behavior or actual iPad execution.

No public Internet reachability claim is made from loopback fixtures. Gateway
behavior is subject to the host's actual routing, proxy policy, sandbox and
local-network consent. External ICMP/raw sockets, netlink, Ethernet/DHCP,
multicast, host inbound forwarding and full Linux tuning/options are outside
this delivery. The subsequent app/SSH/Files milestone remains M4.
