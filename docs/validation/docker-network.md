# Docker 网络验证（2026-10-18）

开发分支 `feat/guest-docker-workflows`，关联 [PR #30](https://github.com/kb5000/trames/pull/30)
和 [Issue #29](https://github.com/kb5000/trames/issues/29)。main/1.0.0/tag 不变，本批不打 IPA。
设计与明确边界见 [网络运行时](../architecture/docker-network-runtime.md)。

本地优化 release runner 已通过原版签名 Alpine Docker 28.3.3-r5 /
containerd 2.1.5-r2 / crun 1.23.1-r0 的 4 vCPU 网络验收：

- 正常 iptables/ip6tables/docker0/ip-forward/ip-masq，无关闭 firewall 的替代配置。
- 容器 localhost/外部 DNS、受控外部 HTTP/UDP，源 NAT 与回包身份。
- 发布 TCP/UDP 端口；根 guest 和独立宿主浏览器式 HTTP/UDP client 均实际收发。
- Docker `127.0.0.11` 名称与 alias、同桥 TCP/UDP、访问已发布端口的 hairpin。
- DOCKER-USER DROP/恢复，跨 bridge 双向阻断，internal 网络拒绝外网。
- 删除容器/网络/镜像、清理 conntrack 无残留告警，实际重新 bind TCP/UDP 原端口。

单独的新磁盘原版 iptables fixture 在 1/4 vCPU 通过 TCP/UDP NAT、可逆 hairpin、
同桥 DROP/恢复、loopback DNAT 回复端口恢复。Net 40、virtual 11、VFS network 9、
Linux network 3/file_calls 3、Core splice 1、FFI managed DNS 1 通过。
unit 覆盖 IPv4/IPv6、ICMP 截短 quote/checksum、zone/冲突/过期/容量与删除、
原生 INPUT/OUTPUT、每包 pktinfo/源地址约束、native/guest ET 和 splice 背压。

最终生产源码：`69fc75b30851916a9ee45e95fb5a6a8d6239060f`。
[Linux 完整验收](https://github.com/kb5000/trames/actions/runs/39103300381) 全部成功，
包含 daemon 重启后的默认 bridge/规则恢复、相同网段/IPAM 重建、严格 DNS 地址
断言和清理/端口重用；原版 Docker 完整场景运行 307.436 秒。该时间与本地
364.774 秒来自不同宿主，不作为性能提升比较。

Apple 的两处宿主测试假设已修正：读取前等待真实 UDP readiness，以及把
60 KB echo peer 的发送缓冲设为 64 KiB（与生产适配器一致），没有缩小测试数据。
`62e1473`、`79326f3` 仅修改测试与 CI，生产代码字节不变。
复用首次 Apple 的 39 项 Net 成功结果，加上
[UDP fixture 修复验收](https://github.com/kb5000/trames/actions/runs/39103832584)
通过的一项，覆盖 Net 40；该轮另通过 10 项 virtual，
[最终 Apple/iOS](https://github.com/kb5000/trames/actions/runs/39104031102)
补过剩余一项 60 KB IPv4/IPv6 native gateway，及 VFS network 9/events 4、
Linux socket 3/file_calls 3、Core splice 1、FFI managed DNS 1 和
`aarch64-apple-ios` FFI check。
[Linux fixture 复验](https://github.com/kb5000/trames/actions/runs/39103832582) 与
[大包 fixture 复验](https://github.com/kb5000/trames/actions/runs/39104031145)
均成功，未重复运行已经成功的完整 Docker/过滤/NAT。

已下载并核验 Linux artifact `guest-container-network` #12127612603，20579 bytes：
SHA256 `58546eb3f82574b1b70b9cb930831e7f978f9683bfab25560dff8680ad91652b`。
ZIP 内三份结构化结果及日志涵盖原版 iptables/filter 1/4 vCPU、NAT/loopback
1/4 vCPU 和原版 Docker 网络 4 vCPU 九组功能；独立宿主 TCP/UDP client 均成功。
CI 与本地 rootfs/APK 指纹相同；不同编译器的 fixture binary 指纹分别保留。
完整输入、原始结果与逐轮复用关系见 [JSON](docker-network.json)。

受影响范围限定 Net、VFS network/events、Linux socket/splice、Core splice
调度、FFI resolver 与 CLI host mapping；CPU/UI/GPU/guest libraries 复用此前通过
结果。早期 [过滤](nftables.md) 和 [内部 bridge](docker-bridge-internal.md)
证据保留其历史范围。完整 Docker 其他工作流仍由 Issue #29 跟进。
