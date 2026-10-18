# Guest nf_tables 与连接跟踪

`NETLINK_NETFILTER=12` 与 route netlink 共用有界传输；端口占用按
(namespace, protocol, port) 区分。FD 保留创建时的 namespace，组播订阅暂不
开放。所有请求需要 guest CAP_NET_ADMIN，不调用宿主 iptables/nft 或修改
宿主网络。没有活动规则时，普通原生 socket 保留快路径；活动规则通过 native I/O 策略检查。

## 控制面

实现 GETGEN、table/chain/rule 的创建、枚举和删除，以及规则插入、替换、
句柄定位、清空、base chain policy、真实 use/ref 计数和包/字节计数器。
空 set dump 返回空，因为当前没有可创建的 set；NEWSET 和单项查询分别
拒绝／返回不存在。原版 libnftnl 删除对象时回送的已知描述属性按 Linux
删除语义处理，不将其误认为新的规则。tcp/udp/icmp/icmp6 revision-0 探测
允许已实现的匹配；另支持 addrtype/conntrack 与 SNAT/DNAT/MASQUERADE/CT zone 的原版 xt wire。未知 revision/flags/扩展仍拒绝。

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

根 native INPUT/OUTPUT、同桥 IP 过滤、分片重组、conntrack/NAT 和原版
Docker 默认 firewall 已接入。ct state/status/mark/zone/tuple、fib addrtype、
标准优先级的 nat/masq 和已实现的 xt 兼容表达式在真实路径求值；NAT 不能安装
进 filter 链，zone 只能在 conntrack 前设置。GET/dump/DELETE 使用实际连接。
相关数据路径、native socket 边界和明确限制见 [Docker 网络运行时](docker-network-runtime.md)。
旧显式 `--virtual-network` 网关模式仍拒绝 base chain，动态子 netns 可独立配置。

sets、flowtable、family bridge/netdev、以太网规则、REJECT 回复、conntrack expiry
match 和未实现的 xt 扩展仍拒绝。IPv6 扩展头与所有内核 defrag/conntrack 细节
不作完整 Linux 等价承诺。dump 将多个对齐消息合并到有界 datagram，避免将
消息数量误当成队列项数量。

## 依据与后续

ABI 对齐 Linux `include/uapi/linux/netfilter/nf_tables.h`、`nfnetlink.h`、
`nf_tables_compat.h`（GPL-2.0 WITH Linux-syscall-note）。参考原版
[iptables 的 nft 后端](https://git.netfilter.org/iptables/tree/iptables/nft.c)
的 batch、chain use 和原版工具 wire 请求；未替换 guest 工具。项目继续使用
GPL-3.0-or-later。本阶段不引入新的第三方实现。

第一阶段 [过滤验收](../validation/nftables.md) 保留其原有范围；后续
默认 firewall、DNS、端口发布和隔离的结果单独记录在
[Docker 网络验证](../validation/docker-network.md)。
