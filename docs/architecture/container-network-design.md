# Guest Docker 网络与隔离路线

目标是原版 Docker 在同一个 Trames Linux guest 内管理容器。继续使用原始
Docker/containerd/crun、GPL 兼容的现有 smoltcp 协议栈和虚拟 Linux 内核对象；
不把宿主 Docker socket 或 iPad 网络权限当作容器实现。

## 已落地的隔离基础

`CLONE_NEWNET` 创建只有 down 状态 lo 的独立协议栈。lo 配置、端口、网络
sysctl、netlink 地址与抽象 Unix 地址属于该 namespace；文件路径 Unix socket
仍按共享文件系统查找。fork、exec、setns、传递的旧 FD 保留其原归属。
`CLONE_NEWIPC` 分离空 mqueue 挂载身份；已有 mount/FD 保留旧对象。SysV IPC
与 mq_* 尚未实现，不能将 namespace 身份支持理解成这些 IPC API 已齐全。
`CLONE_NEWCGROUP` 固定调用者当前 cgroup 为视图根，新 cgroup2 挂载和 proc
路径据此显示，任务实际成员关系及祖先设备/pids 限制不改变。

经典 BPF seccomp 在 guest syscall 入口执行；不可变 filter 随 fork/exec 继承，
支持叠加和 TSYNC。Docker 默认 filter 已实际安装为 mode 2；未使用
seccomp=unconfined 或共享 IPC/cgroup namespace 来通过这一项验收。
完整范围与限制见[验证记录](../validation/container-isolation.md)。

## 已接通的内部数据路径

开发分支已接通 namespace 拓扑、成对 veth、bridge master、跨 netns FD 移动、
IPv4/IPv6 地址和直连/经网关静态路由。独立协议栈由同一有界 fabric 调度，
实际投递沿 up 的 veth 和 bridge 成员关系查找，断开成员或删除地址就停止可达。
TCP/UDP 使用已有 smoltcp IP 层，不引入新的 CPU 锁，也不将所有 netns 全连接。
1/4 vCPU 的真实 guest 数据验收见[内部网络验证](../validation/container-network.md)
与[根网络／转发验证](../validation/container-forwarding.md)。

根 guest 采用双路径 socket：普通目的地址继续走原生 BSD socket，按实际路由
匹配到 guest veth/bridge 的目的地址进入共享协议栈。绑定 bridge 地址使用
guest 监听；根通配 TCP/UDP 端口同时接收原生和 guest 流量，监听后新增 bridge
地址也可访问。普通原生客户端不加入 fabric 等待。接受后的连接直接使用选中
的 backend；公共 socket 额度按逻辑 FD 对象计数，不将双路径重复计数。

IPv4 `ip_forward`／`conf/all/forwarding` 和 IPv6 `conf/all/forwarding` 按 netns
独立，默认关闭。打开对应开关后，按网关路由和真实 bridge/veth 拓扑执行内部
L3 转发，逐跳递减 TTL/hop limit 并更新 IPv4 校验和；关闭后跨桥恢复不通。
耗尽 TTL 的包丢弃，ICMP time-exceeded/unreachable 回复尚缺。旧 sysctl FD
保持打开时的 namespace，写入时重新检查权限。根 eth0 地址快照随查询刷新，
不修改宿主接口；根/子 netns 的 rtnetlink 查询使用同一实际拓扑视图。

这是内部 IP 单播适配；AF_PACKET/以太网帧、ARP、VLAN/STP/multicast、可变 MTU
和 IPv6 scope 尚未开放，相关不支持的设置明确拒绝。新 netns 仍只有 down lo，
必须配置接口。接口/地址/路由及事件队列有上限，根 eth0 继续只读。

## 下一阶段的数据路径

1. 扩展现有根 guest 双路径，接通容器外部转发及 Docker 发布端口所需的
   有状态 NAT；root 没有额外的 10.0.0.1 网关。当前即使 ip_forward=1，也
   不把容器包隐式送入宿主网络。容器访问根 bridge/gateway 地址已有支持，
   访问根原生 eth0/lo 地址服务尚未接通。外部监听仍受宿主 BSD socket、
   iPad 本地网络许可与 App 生命周期约束。
2. 补 NETLINK_NETFILTER/nftables 的规则对象、事务、匹配、计数和连接状态，
   让原版 iptables-nft 的 Docker bridge/NAT 规则真正作用于转发流量。
   不支持的表达式必须拒绝；不能只返回成功 ACK 或以禁用过滤来声称隔离完成。
3. 原版 Docker 的 userland proxy/NAT、嵌入式 DNS、TCP/UDP 端口发布和删除
   清理一起验收，随后验收 pull、volume、exec、build/BuildKit 和 Compose。

控制元数据继续由现有内核协调线程管理。普通 CPU 指令、guest memory 和 musl
原生调用不加入网络锁；接口/路由/规则更新只发生在 syscall 边界。转发工作量与
队列设独立上限，按流缓存的策略必须随路由或规则更新失效，不影响 iPad UI 响应。

## 验收边界

先验证两个容器可监听同一端口、lo 与抽象 Unix 不互通、相同 bridge 可达、
不同网络按规则隔离、外部 DNS/HTTP、宿主浏览器访问已发布 TCP/UDP 端口，
再验证重启/删除/端口重用及权限失败的原子性。网络必须遵守 full/outbound/
loopback/off 的宿主策略，容器不能借 unshare 获得更大外部权限。

当前默认 bridge 的实测阻塞是 `iptables --wait -t nat -N DOCKER` 返回
`Failed to initialize nft: Protocol not supported`。`--network none` 的通过只
验证隔离基础，不代表 bridge/NAT 已完成。[跟踪 Issue #29](https://github.com/kb5000/trames/issues/29)。
