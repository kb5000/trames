# Docker bridge 网络数据路径

本实现位于 `feat/guest-docker-workflows`；已发布的 1.0.0 不包含这些改动。
同一 guest 内由原版 dockerd/containerd/crun 创建网络，宿主不运行 Docker、
iptables、netns，也不修改宿主路由。项目保持 GPL-3.0-or-later。

## 路由、过滤与连接状态

每个 network namespace 持有拓扑、逐族 forwarding、nft ruleset 和有界 conntrack。
原始 IP 分片先重组并校验 transport checksum，再进入 raw/conntrack/NAT/filter；
不会通过重新计算 NAT checksum 把损坏的 UDP/TCP 包变成有效包。

- 本地：raw OUTPUT → conntrack → OUTPUT DNAT/filter → POSTROUTING SNAT。
- 路由：raw PREROUTING → conntrack/DNAT → INPUT，或逐跳 TTL/hop-limit →
  FORWARD → POSTROUTING SNAT。回包按已保存的双向映射恢复地址与端口。
- 同桥：bridge 的 PREROUTING/FORWARD/POSTROUTING 参与 IP 过滤；不递减 TTL，
  不要求 L3 forwarding。`bridge-nf-call-iptables`/`bridge-nf-call-ip6tables`
  独立控制，打开的 sysctl FD 固定 namespace，写入重新校验 CAP_NET_ADMIN。
- loopback 的 OUTPUT/INPUT 保留同一连接附着。`127.0.0.0/8` 为本地网络，
  未显式配置的别名以 `127.0.0.1` 为默认源；Docker 的 `127.0.0.11:53`
  可经原版 iptables 重定向到内置 DNS 的实际监听端口。

IPv4/IPv6 的 SNAT/DNAT/MASQUERADE、端口冲突重分配、连接 mark/zone、
NEW/ESTABLISHED/RELATED/INVALID、方向与原始/回复 tuple 可被规则读取。
NAT 只在首包建立，后续包继续应用绑定并重新检查 filter；删除 NAT 规则不会
错误地改变已有连接。ICMP 错误的截短 quote 按增量 checksum 还原；FIN/RST
缩短回收时间，单向/双向流超时与 namespace/address 清理回收映射。

NETLINK_NETFILTER conntrack GET/dump/DELETE 返回真实连接，支持原版 Go netlink
将完整 dump 属性原样回送 DELETE。权限、namespace、zone、tuple、mark/mask、
id 和完整回复容量在提交前校验。单 namespace 最多 512 个连接；过期扫描每秒
至多一次，NAT 地址/端口预览不改变正式计数器或占用连接。

nft 新增 ct/fib/nat/masq 和原版 iptables 使用的 xt addrtype、conntrack、
SNAT/DNAT/MASQUERADE/CT zone。NAT/zone 的合法 hook 和优先级在事务提交时检查，
不支持的表达式仍返回 EOPNOTSUPP。见 [nf_tables](nftables.md)。

## 宿主边界与端口发布

根 guest 的普通 socket 保留 BSD 路径；guest 设备路由或 OUTPUT 重定向走
共享 smoltcp。容器出站必须经过 forwarding/filter 和实际源 NAT 绑定，才可
由有界 native TCP/UDP/ICMP 适配器访问宿主网络。`off/loopback/outbound/full`
策略继续有效，unshare 或设置 forwarding 不会自动扩大宿主网络权限。

根 native socket 的 connect/accept/send/recv 同样检查 namespace 策略；按
逻辑 SYN/消息/回复执行过滤，而非宣称接管了宿主内核的 TCP 重传、序列号和
以太网帧。宿主 BSD socket 实际处理 TCP 传输。无活动规则时跳过策略求值。

默认端口发布使用原版 **docker-proxy**：TCP/UDP wildcard listener 同时覆盖
宿主入口和 guest 设备；每个 UDP peer 分别保存回流策略。IP_PKTINFO/
IPV6_RECVPKTINFO 获取每个 datagram 的实际目的地址，sendmsg source control
只允许该 namespace 自己的地址/设备。Darwin 与 Linux 的 native cmsg 显式
转换，不透传 guest struct、host interface index 或指针。

Go 网络轮询所需的 EPOLLET 由 Linux epoll/Darwin kqueue 的真实边沿和 guest
栈的事件代数支持，监视器按需创建。`splice` 支持经管道传输 socket/file 数据，
输出背压时不消费源管道；它不宣称为宿主页面零拷贝。

宿主浏览器访问 iPad 自身地址和 `-p HOST:CONTAINER` 发布端口，仍受 iPadOS
网络许可、前后台运行规则和网络连通性约束。**保留默认 userland-proxy=true**；
`--userland-proxy=false` 尚没有自动创建宿主入口 listener，不能作为可用替代。

## DNS

托管 `/etc/resolv.conf` 使用 `169.254.0.53`，这是可路由的系统 DNS 服务地址，
不是 guest 网关或宿主真实接口。容器请求经过真实转发/NAT/回包路径，随后由
有界 worker 调用宿主系统 resolver；A/AAAA 分别解析，临时错误返回 SERVFAIL。
根 loopback-only 模式仍使用 `127.0.0.53`。只迁移精确的已托管默认值，保留
用户自定义文件。Docker 自定义网络的名称/alias 由其原版 `127.0.0.11` resolver
实现，不在 Trames 里伪造容器目录。

## 边界

不等同于完整 Linux 内核网络：sets/flowtables/所有 xt 扩展、AF_PACKET/原始
以太网/ARP、STP/qdisc/tc、完整 IPv6 扩展头与所有 ICMP 类型、完整 conntrack
TCP 状态机、counters/protoinfo dump 等尚未覆盖。正常 IPv6 出站需要宿主有
相应地址/连通性；IPv4 环境不伪造可用 IPv6 网关。地址/连接/socket/fabric
仍有显式资源上限，达到限制返回错误。纯文档/验证记录更新不重复运行验收。

验收和准确的源码/二进制/输入指纹见 [Docker 网络验证](../validation/docker-network.md)。
