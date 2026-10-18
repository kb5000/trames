# nftables 过滤验收（2026-10-18）

开发分支，不修改 main／1.0.0，不构建 IPA。实现范围见
[设计](../architecture/nftables.md)，结构化输入与结果见 [JSON](nftables.json)。

本地 Linux release runner：Net 34、virtual 11、strict clippy／fmt 通过。
原版 Alpine 签名 iptables 1.8.11-r1／libnftnl 1.2.9-r0 在新磁盘上安装，
1／4 vCPU 分别通过完整 C fixture：实际 routed IPv4／IPv6 source/prefix、
interface、UDP port 匹配，DROP／ACCEPT、insert／replace／check／delete／flush／
policy，实际计数器与独立 namespace INPUT。另验证已建立 TCP 被 FORWARD
策略阻断；无匹配流量不受阻，撤销规则恢复。事务回滚、权限、旧 generation、
回复容量、非法寄存器、循环跳转、协议端口隔离和原生 hook 拒绝有单元覆盖。

受影响的现有 forwarding/TTL/root-native C fixture 1／4 vCPU 同样通过。
CPU／图形／VFS／Linux/Core 以及原版 Docker daemon/镜像/内部 bridge HTTP 的
未变流程直接复用 [上一阶段记录](docker-bridge-internal.md)，没有重跑全套。

Linux／Apple CI 待本次源码提交后记录；当前不声称已完成远端验证。

测试中 conntrack 和 NAT 的 EOPNOTSUPP 是预期的拒绝，退出码来自 C fixture
逐项核验后的 0，不把拒绝当作功能实现。没有 default Docker firewall、
conntrack/NAT、外部 DNS/HTTP、发布端口或同桥 br_netfilter 的通过结论。
