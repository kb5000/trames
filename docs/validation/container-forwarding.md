# 根 guest 与容器通信、内部 L3 转发验证

2026-10-18，`feat/guest-docker-workflows` 开发分支，延续
[内部 veth/bridge](container-network.md)与[默认隔离](container-isolation.md)。
本批未合入 main，未改动 1.0.0，也没有生成 IPA。

## 实现范围

- 根 guest 普通连接继续使用原生 BSD socket；按实际路由访问 veth/bridge
  则使用既有 smoltcp 栈。根 bridge 地址可监听，TCP/UDP 通配绑定同时接受
  原生与 guest 流量，支持先监听后配置地址。根 UDP 在原生／guest／原生间
  切换保持源端口，多个 bridge 选择实际出口地址。接受后的 TCP 直接使用
  对应 backend。普通客户端没有额外 fabric 等待。
- 逻辑 socket 额度包含 netlink 与 accept，双路径计为一个 socket；绑定
  一侧失败会回滚另一侧。延迟重建原生 descriptor 时保留协议身份和低端口
  权限检查。选项记录、拓扑和栈仍有独立预算。
- IPv4 ip_forward 与 conf/all/forwarding 共用本 netns 开关，IPv6 独立。
  默认关闭，不继承进新 netns；旧 sysctl FD 固定原 namespace，write 重新
  检查权限。IPv4/IPv6 按路由、网关及 up 的 veth/bridge 成员逐跳转发，
  正确递减 TTL/hop limit、更新 IPv4 头校验和，环路与 TTL 耗尽有界丢弃。
- 根网络地址查询刷新原生 eth0 快照，保留 guest 配置；根与子 netns 使用
  统一 rtnetlink 查询，支持 Go netlink 所需的 RTM_SETLINK 更新。根 eth0
  仍是只读视图，不操作宿主接口。

## 验证

本地增量检查已通过严格 clippy、fmt，24 项 Net 单测、121 项 VFS 单测
（另 1 项原有 ignored）、受影响的 Linux 网络单测及 11 项既有 virtual
网络测试。代码不变的早期通过项直接复用，不为记录更新重复运行。

真实 ARM64 静态 C fixture 通过 `trames-run --release` 执行；
`container-forwarding`、`container-network`、`network-namespaces` 三个程序
各验证 1/4 vCPU。新程序验证根通配监听的原生／guest TCP、root UDP 的
实际出口源地址、双 bridge IPv4/IPv6 路由、默认关闭／开启／再次关闭、
旧 sysctl FD 的归属与 TTL=1 丢弃、TTL=2 成功。

新磁盘上原版签名 Alpine Docker 28.3.3-r5/containerd 2.1.5/crun 1.23.1
严格隔离 1 vCPU 回归通过：daemon、导入、create/start、PID 1、私有
net/IPC/cgroup、seccomp mode 2、退出码 7、logs、删除及清理。
daemon 仍禁用 bridge/iptables，容器为 network=none；该项不验证 NAT。

Linux 与 Apple/iOS FFI 对应 CI 待本批源码提交后完成；完整输入指纹、
本地结果和后续 CI 链接写入[机器记录](container-forwarding.json)。

```sh
bash scripts/build-container-network-fixture.sh
cargo build -p trames-cli --release --locked
python3 tests/integration/container-network.py
python3 tests/integration/docker.py --isolated --require-run --cpus 1
```

CI 仅验证受影响的 Net/VFS/Linux、既有 virtual 网络、真实 guest 网络、
原版 Docker 4 vCPU 与 ARM64 iOS FFI。Core/seccomp/CPU/UI/GPU/guest 库
源码未变，复用之前的验收；不重新编译 IPA。

## 仍未实现

本批仅接通内部 L3。即使 ip_forward=1，容器包也不会隐式进入宿主网络。
NETLINK_NETFILTER/nftables、conntrack、NAT、Docker 默认 bridge、外部
DNS/HTTP、嵌入式 DNS与发布端口仍待下一阶段。访问根 bridge/gateway
服务已有支持，访问根原生 eth0/lo 地址的服务尚未接通。

当前 IP 单播适配仍不提供 AF_PACKET/原始以太网、ARP 抓包、VLAN/STP/
multicast、可变 MTU、IPv6 link-local scope，也没有生成转发 ICMP
time-exceeded/unreachable。peer carrier/自动路由通知、root 双路径持续
满负载公平性和复杂 connected UDP 重绑定语义仍需扩展。

跟踪 [Issue #29](https://github.com/kb5000/trames/issues/29)，草稿
[PR #30](https://github.com/kb5000/trames/pull/30)。
