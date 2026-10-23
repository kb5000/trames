# 原生 TCP 的工作线程字节 I/O

2026-10-22，第十三批协调器拆分。已连接的无活动防火墙规则原生 TCP，在 worker
执行 read/write/readv/writev；IPv4/IPv6 与 owner 入口共用实际 BSD 收发主体。
真正阻塞、信号、其余网络调用仍用原内核路径。[设计](../architecture/epoch-services.md)。

## 验证

18 项不同单测通过：native 7（其中新增共享 TCP 4）、防火墙新旧路径各 1、root
routing 7、VFS 1、Linux ABI 1。覆盖同一 socket 的 owner/worker 交错读写、PEEK
不消费也不计流量、共享原子计数与饱和、EOF、并发 writer 的 Send/Sync、最后一次
关闭不被旧 capability 延迟、无 FD 变更的 connect 完成后刷新能力、活动 ACCEPT
chain 也拒绝快路径及删除后恢复、无效地址不消费字节、向量 ABI、阻塞 EAGAIN
checkpoint 和非阻塞 EAGAIN 本地完成。

另通过现有 native gateway 的 TCP/UDP、IPv4/IPv6 集成项，确认共用收发主体与
原子统计未破坏原网络传输。Release CLI/FFI、相关严格 Clippy（含 tests）、格式/
脚本语法/差分检查通过。不重跑完整 GPU、浏览器、Docker、CPU 差分或 CoreMark。
上一轮单活跃进程 MMU 修复的准入函数未改，继续复用其验证。

## 相同 musl ELF 前后对照

原版静态 ARM64 musl 程序连接独立宿主 loopback TCP 服务，一个线程发送 128×1024
字节，另一个线程持续原生 memset。宿主逐字节核对每个 round 的数据，接收 FIN 后
回传成功标记。每次运行使用新的端口，无外网连接或下载。相同 C 程序在宿主原生
运行作为输出参考；多 CPU 的 worker NativeCall 无 fault/fallback/rejected。

write、writev × IPv4、IPv6 × 2、4 CPU，以及 IPv4/write 的 1 CPU owner 对照，
修改前后共 18 次 guest 运行全部通过，4 个原生参考通过。基线是已含共享 futex、PTY
和单活跃进程 MMU 修复的上一轮开发代码，并非已发包 build 36。多 CPU 新路径每次完成
129 次本地 TCP 调用、服务请求为 0；不把原生 peer 的不定迭代数当固定工作量。

| 模式 | CPU | 修改前整组返回 | 修改后 | 本地 TCP 调用 |
| --- | ---: | ---: | ---: | ---: |
| IPv4 write | 2 | 133 | 6 | 129 |
| IPv4 write | 4 | 134 | 6 | 129 |
| IPv4 writev | 2 | 134 | 7 | 129 |
| IPv4 writev | 4 | 133 | 4 | 129 |
| IPv6 write | 2 | 133 | 7 | 129 |
| IPv6 write | 4 | 133 | 7 | 129 |
| IPv6 writev | 2 | 134 | 4 | 129 |
| IPv6 writev | 4 | 134 | 7 | 129 |

单 CPU 两版均不使用 cohort，计数为 0。这里的返回数受 host readiness、启动/关闭
边界和调度影响，属于单次结构样本，不是吞吐跑分，也不能换算为 Docker、Firefox
或 iPad 加速比。Apple ARM64/16 KiB、iOS 及真机结果尚未执行。

能力不携带 Rc/RefCell 的 namespace 或整个 VFS；不复制宿主 socket，不延长最后一次
close。连接或防火墙变化后重新建立能力，不把瞬时允许决定缓存到后续 cohort。
有规则的收发仍保留原 policy/conntrack/计数路径。

外部网络阻塞的直接唤醒、UDP、网络 poll/epoll、sendto/recvfrom/sendmsg/recvmsg、
connect/accept/shutdown/options 尚未迁移。没有为这些等待增加服务线程、轮询，
也没有让 guest worker 等待一个不能接收宿主网络通知的 condition variable。

复现入口：`scripts/build-worker-tcp-fixture.sh`、`tests/integration/worker-tcp.py`。
后者接收 `--runner`、`--baseline`、`--oracle`、`--output`；原始日志保留在
`target/worker-tcp-checks/`。公开摘要为
[worker-tcp-summary.json](worker-tcp-summary.json)，只记录必要统计和源码/产物哈希。
