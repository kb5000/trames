# Guest veth / bridge 内部网络验证

本批在 1.0.0 后的 `feat/guest-docker-workflows` 开发分支中，延续
[隔离基础验收](container-isolation.md)。它未合入 main、未生成新 IPA。

## 已实现

- 同一 guest 的 netns 共用有界拓扑和现有 smoltcp IP 协议栈；每个 netns
  的 lo、设备、地址、端口、路由和 socket 归属仍独立。共享 fabric 不提供
  隐式的全连接网络，不能借新建 netns 扩大 embedding 的网络权限。
- NETLINK_ROUTE 的 veth 成对创建/删除、peer NET_NS_FD、移动、重命名、
  up/down、bridge 创建/删除和 master 附着/解除。先验证整个创建/移动再提交；
  失败不留下半个 veth。最后一个 namespace 引用释放时清除接口和对端。
- IPv4 / IPv6 地址、直连路由、无网关静态路由及手工路由记录的增删；
  地址/接口状态改变真正影响收发。不同桥在目前未开放 L3 转发时互相隔离。
- TCP / UDP 数据走真实的现有协议栈，保留重传、校验和和分片实现；支持
  先 wildcard bind/listen 后配置地址。同一 bridge 的 IPv4 ICMP echo 也已检查。
- GETLINK/GETADDR/GETROUTE、链路/地址/路由 multicast 订阅和有界溢出提示；
  `/proc/PID/net`、`/sys/class/net` 与接口 ioctl 反映目标 namespace 的配置。
  sendto/sendmsg/write 解析的是调用者 guest FD 表中的 nsfs 对象，不传宿主 FD。
- 控制配置由已有内核协调线程管理，没有给 CPU / guest memory / musl 加锁。
  地址配置按 revision 更新协议栈；监听句柄、链路、路由、地址和事件队列有上限。

## 本地检查

- Net library 16 项、VFS 120 项（1 项原有 ignored）、Linux library 21 项通过。
- 既有 virtual 网络 11 项通过，涵盖 TCP/UDP/IPv6、DNS、宿主 gateway、
  分片、丢包和配额。
- strict clippy：Net/VFS/Linux；cargo fmt 通过。
- `container-network.c` 在真实 ARM64 guest 1/4 vCPU 中通过：创建 bridge/veth、
  peer namespace FD、配置 IP、先监听后配置、TCP/UDP 数据、跨桥隔离、移出桥
  后断路/重接后恢复、旧 netlink FD 与当前调用者 namespace 不同、proc/sysfs/ioctl。
- 既有 `network-namespaces.c` 在新共享 fabric 下的 1/4 vCPU 验收通过。
- 原版签名 Docker 的 `--network none` 默认安全配置在新磁盘 1 vCPU 下通过：
  daemon/image/create/start/logs/cleanup、退出码 7 和各项隔离断言全部检查。
- [Apple 定向 CI](https://github.com/kb5000/trames/actions/runs/39091721951) 通过：
  Net/VFS/Linux library，以及 `aarch64-apple-ios` 的 trames-ffi check；没有链接 App 或 IPA。
- [Linux 定向 CI](https://github.com/kb5000/trames/actions/runs/39091722010) 的 library/virtual
  检查以及 guest/Docker 4 vCPU 检查通过。基线源码 `01a87156c44cb10b5c2c2993186ce4274f058ed6`；
  同目录 `container-network.json` 保存最终结果和输入 hash。

补充生命周期回归先复现“只有旧 netlink FD 时 IP 栈提前释放、ping EAGAIN”，
随后通过 namespace 强持有 stack 修复。最后引用释放先结束协议栈再删除拓扑，
不存在身份/协议栈所有权环。6 项 routing case、既有 teardown case 和
guest 网络/旧 netns 1/4 vCPU 已再次通过；CI 仅补这一精确增量和 iOS FFI，
复用通过的基线 Docker/其他 library/virtual 检查，不重复无关验收。

首次本地 native socket 检查被执行沙箱的网络权限拒绝；使用环境支持的网络权限
重跑失败项目后通过，已通过项目复用。旧 root-ifindex 测试将不存在的接口错误从
笼统 EOPNOTSUPP 更新为 ENODEV；没有把拒绝配置改成成功。

## 仍未实现

这是一条经过验证的 **内部 IP 单播链路**，尚非完整 Docker bridge：

- 根 guest 的容器地址路由、桥地址上的本地服务、跨 namespace 的 L3 转发、
  容器外部 DNS/HTTP、NETLINK_NETFILTER/nftables、NAT 和端口发布仍未接通。
  `ip_forward` 仍是 0；不能删除原版 Docker 验收的 bridge/iptables 禁用选项。
- 默认路由/网关记录可以创建和查询，网关转发不因此自动开放；不提供假成功
  的 nftables 规则，也不复用旧 fabric 的固定 10.0.0.X 作为容器 IP。
- 数据适配位于 IP 层，尚不提供 AF_PACKET、原始以太网帧、ARP 抓包、VLAN、
  STP、multicast 或链路本地 IPv6 scope。MTU 当前固定 1500，其他设置明确拒绝。
- 根逻辑 eth0 继续只读，普通 root socket 直连宿主。旧显式 Fabric API 保留
  已验证行为，不能用于绕过新拓扑隔离。暂无物理 iPad 网络或性能提升结论。

## 重现

```sh
bash scripts/build-container-network-fixture.sh
cargo build -p trames-cli --release --locked
python3 tests/integration/container-network.py
TRAMES_ALPINE_CACHE=target/alpine-download python3 tests/integration/docker.py --isolated --require-run --cpus 1
```

交叉编译器可通过 `AARCH64_CC` 指定。后两步需要允许本地 socket 的执行环境。
持续跟踪：[Issue #29](https://github.com/kb5000/trames/issues/29)。
