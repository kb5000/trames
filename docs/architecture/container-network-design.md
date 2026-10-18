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

## 下一阶段的数据路径

1. 每个 netns 保存真实的接口、地址、路由和 namespace 引用；rtnetlink 的
   create/delete/move/rename/up/down 先验证，再原子更新这些对象。veth 两端、
   bridge master、netns FD 与销毁后的清理必须保持同一身份。
2. 容器端使用可配置 CIDR 的现有软件协议栈；veth/bridge 决定实际可达性，
   独立 lo、相同端口及不同 bridge 的隔离不能被一个全连接 fabric 代替。
   不复用旧 API 的固定 10.0.0.X 地址作为 Docker 容器地址。
3. 根 guest 的普通 socket 继续直连宿主；访问容器地址或 Docker 发布端口时，
   根据实际路由和 NAT 进入容器数据路径。root 没有额外的 10.0.0.1 网关。
   外部监听仍受宿主 BSD socket、iPad 本地网络许可与 App 生命周期约束。
4. 补 NETLINK_NETFILTER/nftables 的规则对象、事务、匹配、计数和连接状态，
   让原版 iptables-nft 的 Docker bridge/NAT 规则真正作用于转发流量。
   不支持的表达式必须拒绝；不能只返回成功 ACK 或以禁用过滤来声称隔离完成。
5. 原版 Docker 的 userland proxy/NAT、嵌入式 DNS、TCP/UDP 端口发布和删除
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
