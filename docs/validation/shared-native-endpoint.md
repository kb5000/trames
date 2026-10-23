# 共享原生 socket 端点与等待寿命

2026-10-23，第 29 批协调器拆分。原生宿主 FD、配额租约和持久边沿观察器已从
owner Socket 的 Rc 控制对象中拆出，真实端点最后引用可以在 worker 释放。
本批为后续动态关闭/替换建立资源所有权，没有新增 worker syscall 或宣称整体加速。
机器摘要见 [shared-native-endpoint-summary.json](shared-native-endpoint-summary.json)。

## 实现与边界

- `Arc<Endpoint>` 持有宿主 TCP/UDP socket、惰性创建的边沿观察器和配额租约。
  创建/accept 前原子预留，失败自动回收；先关闭实际 FD，最后归还配额。计数只用
  relaxed 次序，不发布 guest 数据或叠加普通访存屏障。
- 已接受的 connected TCP 操作可以取得 `TcpLease`；DirectTcp 快照仍是弱引用。
  FD 关闭、别名释放或数字 FD 重用不会把仍在进行的操作指向另一连接；普通快照
  不延长 FIN。完整 OFD 仍有 owner 引用，这一步没有直接迁出 guest close/dup。
- 串行入口和 worker 共用一个 epoll/kqueue 边沿观察器，按 socket 对象锁采样。
  不把重复注册 level-ready FD 当成新边沿，TCP 字节读写不获取观察器锁。
- 普通阻塞 I/O 与 io_uring 共用宿主等待捕获入口。等待注册的 duplicate FD
  同时保留原端点；取消先从宿主后端注销，然后在 registry 锁外关闭 duplicate
  并释放资源。注册失败也按此顺序回收。直接转换现有 Arc，不增加堆包装；重试
  继续复用原注册/租约，不新增服务线程或轮询机制。

原生 Socket 控制投影、DNS/规则/连接状态、guest namespace 的 socket 控制计数
仍在 owner；这里迁出的配额是宿主 transport 配额。完整 FD/OFD、Unix SCM_RIGHTS、
PTY 最后关闭和 VM/task 生命周期仍需继续迁移，不能移除其现有 join 边界。

## 验证

24 项不同的 release 定向 Rust 检查通过：net 12、shared 5、VFS 2、Linux 5。
覆盖并发预留不超限、失败 accept 回滚、worker 最后释放回收配额、FIN/半关闭 EOF、
dup/fork 后全部关闭与数字 FD 重用、弱能力不保活、等待取消/失败析构顺序、锁外
析构、多个等待者及重试注册复用，以及跨 owner/worker 持久边沿观察。原防火墙
能力刷新、双栈与混合网络绑定失败配额检查也通过。

原静态 ARM64 TCP fixture 的源码与 ELF 哈希未变；重新生成的宿主 oracle 与
第 14 批记录完全同哈希，复用其四份通过输出，不重复执行参考测试。新 release
runner 跑过 IPv4/IPv6 × 标量/向量 × 1/2/4 CPU，共 12 组。覆盖真实 loopback
宿主服务、发送背压、分段回复和最终 EOF。双核/四核各完成 133 次 worker TCP
调用、14–15 次等待，全部服务转交请求为 0；单核保持串行路径。统计用于确认路径，
不作为应用吞吐或协调器全部撤除的结论。

Release CLI/FFI 构建、五个相关 crate 的严格 release Clippy（含测试）、格式和
差分检查通过。原始记录留在本地 `target/shared-native-endpoint-checks/`；没有
重跑无关 CPU、图形、浏览器、Docker 或 CoreMark，没有制作 IPA、提交或发布。
当前验证环境为 Linux x86_64，Apple kqueue/ARM64 和 iPad 仍待后续构建验收。

复现 guest 检查：

```sh
python3 tests/integration/worker-tcp.py --blocking \
  --runner target/perf-runtime/release/trames-run \
  --oracle target/shared-native-endpoint-checks/native-worker-tcp-dynamic \
  --cpus 1 2 4 \
  --reuse-oracles target/worker-tcp-waits-checks/blocking-final \
  --output target/shared-native-endpoint-checks/guest-recheck
```
