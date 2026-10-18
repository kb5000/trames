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

本地完整 Docker 运行 364.774 秒。该结果尚不包含后来新增的 daemon 重启、
DNS 结果 IP 断言、只读 STP 状态与最终 Core nonblocking 分支；这些项目及最终
源码的 Apple/iOS 检查由下述 CI 单独核验，不将本地旧 binary 扩称最终源码。
当前 CI 状态：待运行。

所有 rootfs/APK、fixture binary/source 与脚本指纹及原始结果见
[结构化记录](docker-network.json)。最终 CI artifact 的 SHA256 与源码提交将
在验证结束后补入，不因记录更新重跑未变的检查。

受影响范围限定 Net、VFS network/events、Linux socket/splice、Core splice
调度、FFI resolver 与 CLI host mapping；CPU/UI/GPU/guest libraries 复用此前通过
结果。早期 [过滤](nftables.md) 和 [内部 bridge](docker-bridge-internal.md)
证据保留其历史范围。完整 Docker 其他工作流仍由 Issue #29 跟进。
