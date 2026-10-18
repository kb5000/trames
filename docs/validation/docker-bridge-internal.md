# 原版 Docker 内部 bridge 与控制 ABI

2026-10-18，`feat/guest-docker-workflows`，延续
[根 guest／L3 转发](container-forwarding.md)。本批不合入 main，不改动
1.0.0/tag，也不生成 IPA。

## 实现

- Linux NLA_STRING 接受带或不带结尾 NUL 的类型／接口名；保留长度、内嵌
  NUL、名称冲突和授权检查。原版 Go netlink 的 create、rename 均可执行。
- IFLA_TXQLEN 在 bridge/veth/peer 创建和更新时保存，netlink/sysfs 返回同一
  元数据，随设备移动保留；失败的名称／属性更新不部分提交。根 eth0 仍只读。
  队列元数据不分配相应大小的缓冲区，不代表有 qdisc/tc。
- BusyBox rtgenmsg 的 1／4 字节紧凑 link/address/route dump 与完整请求一致，
  不将短报文当作可变更配置的请求。
- 订阅 socket 可按 Linux 语义向 kernel+首个目标组发送请求。组播要求
  NET_ADMIN，只投递本 namespace 内对应组的其他订阅者；用户来源端口和
  kernel 回复／通知分开记录在每个 datagram 上，peek／后续读取不串来源。
  队列／字节额度及 overrun 保留，任意 peer unicast 仍未开放。
- RTM_GETROUTE 根据实际 namespace、up、前缀／metric、网关与出口源地址
  返回结果；无路由明确报错，不为 libnetwork 伪造出口。
- bridge brif 成员链接和 brport/state 反映实际 master、up 和 carrier，
  detach 后消失；尚无 STP/VLAN／原始以太网。
- 原生与 smoltcp TCP/UDP IPv6 socket 默认双栈。修复 BusyBox nc 监听 `[::]`
  时拒绝 IPv4 的问题；显式 IPV6_V6ONLY=1 仍有效，ICMP 保持单协议。

## 验证范围

本地严格 clippy/fmt、Net 29 单测、VFS network 5 单测、既有 virtual 11 项
通过。新静态 ARM64 C fixture 在 1/4 vCPU 通过真实 Linux ABI 的 Go 创建／
rename／队列、BusyBox dump、订阅、RouteGet 与 bridge sysfs 状态；旧三组
内部数据 fixture 和 Linux/Core/CPU/UI/GPU 源码不变，复用上一批对应证据。

已使用原版 Docker 28.3.3-r5/containerd 2.1.5/crun 1.23.1，在开发测试磁盘
上完成 bridge 创建、veth/netns/eth0/IP/网关、容器启动、退出码 7、日志与清理。
新增的新磁盘测试进一步检查默认私有 net/IPC/cgroup、seccomp mode 2、容器
ping 网关、根 guest 获取容器 BusyBox nc 的 HTTP，新磁盘 1 vCPU 已全部通过。
生产源码 `27f2931be47abc13e5d66558358a356e2e4a9c6b` 的
[Linux](https://github.com/kb5000/trames/actions/runs/39096972765)／
[Apple](https://github.com/kb5000/trames/actions/runs/39096972663) CI 均成功：Apple 通过 Net 29、VFS network 5 和 ARM64 iOS FFI；Linux 通过
严格 clippy/fmt、Net 29、VFS network 5、virtual 11、新控制 ABI 1/4 vCPU，
以及新磁盘原版 Docker internal-bridge 4 vCPU 的网关 ICMP、真实 HTTP 与
完整生命周期。最后记录提交只更新文档／hash，不重复触发构建或测试。
最终源码和输入指纹见[机器记录](docker-bridge-internal.json)。

```sh
bash scripts/build-docker-network-fixture.sh
cargo build -p trames-cli --release --locked
python3 tests/integration/container-network.py --docker-control-only
python3 tests/integration/docker.py --internal-bridge --cpus 1
```

CI 针对 Net、VFS network、既有 virtual、上述新 fixture 1/4 vCPU、原版
Docker internal-bridge 4 vCPU、ARM64 iOS FFI。重用上一轮通过的无改动
Linux/Core/seccomp/CPU/UI/GPU/guest 库与 network=none 验收；不因记录更新重复运行。

## 仍然缺少

测试 daemon 明确使用 `iptables=false`、`ip6tables=false`、`ip-masq=false`。
内部 IPv4 bridge 运行不代表完整默认 Docker bridge：NETLINK_NETFILTER／
iptables-nft、conntrack、过滤、NAT、外部 DNS/HTTP、嵌入式 DNS、发布端口
以及完整 Docker IPv6 网络还未验收。该配置没有 Docker 防火墙／跨桥过滤，
ip_forward=1 时路由可达的网络可以互通；不隐式将流量送往宿主外网。

原始 AF_PACKET/ARP 广告仍由原版运行时提示不支持；本批不伪造 ARP 响应。
更多限制保留在[转发范围](container-forwarding.md)。跟踪
[Issue #29](https://github.com/kb5000/trames/issues/29)，草稿
[PR #30](https://github.com/kb5000/trames/pull/30)。
