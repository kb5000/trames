# Guest nf_tables 第一阶段

`NETLINK_NETFILTER=12` 与 route netlink 共用有界传输；端口占用按
(namespace, protocol, port) 区分。FD 保留创建时的 namespace，组播订阅暂不
开放。所有请求需要 guest CAP_NET_ADMIN，不调用宿主 iptables/nft 或修改
宿主网络。普通原生 socket 的快路径不进入此过滤器。

## 控制面

实现 GETGEN、table/chain/rule 的创建、枚举和删除，以及规则插入、替换、
句柄定位、清空、base chain policy、真实 use/ref 计数和包/字节计数器。
空 set dump 返回空，因为当前没有可创建的 set；NEWSET 和单项查询分别
拒绝／返回不存在。原版 libnftnl 删除对象时回送的已知描述属性按 Linux
删除语义处理，不将其误认为新的规则。tcp/udp/icmp/icmp6 revision-0 探测
仅允许可转成下述表达式的匹配；opaque xt match/target 仍拒绝。

单个 datagram 的 batch begin/end 包围一个事务：复制 namespace 的规则集、
校验所有请求和跳转关系、检查完整回复容量，最后发布。失败只返回错误 ACK，
已有对象、规则、计数器和 generation 不变。GETGEN／NFNL_BATCH_GENID 防止
读后更新覆盖其他成功提交，旧 generation 返回 ERESTART。只读的空 batch
用于原版 iptables -C 的 generation 确认；不改变 generation。

每 namespace 最多 16 tables／64 chains／512 rules；每规则最多 32 expressions，
名称为小于 256 bytes 的 UTF-8 NUL 字符串，userdata 最多 256 bytes；计费后的
规则存储最多 256 KiB。传输沿用 1 MiB 请求、128 个消息、128 个队列项／256 KiB
回复上限。分割到多个 datagram 的事务、通知、ECHO、对象 rename、sets、flowtable、
动态更新和未知属性显式拒绝。跨消息引用 ID 尚缺，使用已命名的链和 handle。

## 规则与数据路径

按 Linux UAPI 编码实现 immediate、meta、payload、cmp、boolean bitwise、counter。
支持 IPv4／IPv6 地址和网络前缀、L4 协议、端口、接口 index/name 和包长，
以及 ACCEPT／DROP／JUMP／GOTO／RETURN。payload 限 network/transport header；
meta 只接受已实现的 key；寄存器读必须在同一规则中已初始化。跳转图在提交时
验证：不能跳入 base chain、不能成环，深度最多 16，每条链最坏工作量不超过
8192 条规则。计数器在真实匹配位置更新，dump 返回当前值。

独立 namespace 的 IP 栈执行 OUTPUT → POSTROUTING；每个内部路由 hop 执行
PREROUTING → TTL/hop-limit 递减 → FORWARD → POSTROUTING；目标执行
PREROUTING → INPUT。每个 hook 按优先级遍历同族或 inet base chain；一个
base chain 的 ACCEPT 不能绕过稍后的 DROP。拓扑、up、路由和逐族 forwarding
仍决定可达性，过滤不会创建连接或启用宿主出口。没有 active hook 时跳过规则
求值和接口名分配；CPU／musl 不新增锁。

根 guest 的 native BSD socket 不经过 smoltcp，因此根只允许 FORWARD base
chain，其余 hooks 返回 EOPNOTSUPP。旧的显式 `--virtual-network` 网关模式
也拒绝安装 base chain；其动态子 netns 可独立配置。不能通过成功安装一个
根 INPUT／OUTPUT 链来假装已过滤 native I/O。

当前没有 conntrack／RELATED／ESTABLISHED／NAT／masquerade／reject 回复，
也没有 br_netfilter 的同桥 IP 过滤、family bridge/netdev 或以太网规则。
本阶段以内部 routed 单播为验收范围；访问本机非 loopback 接口地址时，
local-route 的输入／输出设备视图仍需完善。过滤作用于 fabric 的 IP 包；
尚未在 hooks 前统一重组分片，基于 L4 的规则
不能承诺 Linux defrag 的全部语义。IPv6 扩展头读取有界，无法解析的 transport
匹配不成立；这些限制都不是完整 Docker 默认 bridge 的验收。

## 依据与后续

ABI 对齐 Linux `include/uapi/linux/netfilter/nf_tables.h`、`nfnetlink.h`、
`nf_tables_compat.h`（GPL-2.0 WITH Linux-syscall-note）。参考原版
[iptables 的 nft 后端](https://git.netfilter.org/iptables/tree/iptables/nft.c)
的 batch、chain use 和原版工具 wire 请求；未替换 guest 工具。项目继续使用
GPL-3.0-or-later。本阶段不引入新的第三方实现。

下一步补连接跟踪和 NAT，并将 root native I/O、外部转发、发布端口接入相同
策略模型，再开放 Docker 默认防火墙配置。原版 Docker 内部 bridge／HTTP 的
[已有证据](../validation/docker-bridge-internal.md) 保持原验收范围；本阶段
[独立过滤验收](../validation/nftables.md) 不将它扩称完整 Docker 网络。
