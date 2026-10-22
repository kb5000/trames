# Unix stream 移出协调服务

> 本文为公开的验证汇总。提及的原始日志、二进制和逐项 JSON 记录保留在开发环境，未随本次源码提交公开。

2026-10-22，第七批协调路径拆分，接续[普通文件 worker 路径](worker-files.md)。
本轮继续在本地工作树实现，没有新增宿主服务线程、推送或构建 IPA。

## 本轮实现

- AF_UNIX stream 的双向字节队列、容量、端点存活和半关闭状态由原 VFS 与 worker
  共用 Arc 资源。channel 对象锁保护相关不变量，不加 VFS 全局锁，也不改 guest 字节
  的弱内存模型。字节数在相同事务内维护，FIONREAD 不再扫描队列。
- 当前 worker 执行 read/write/readv/writev、无 FD 权限传递的
  sendto/recvfrom/sendmsg/recvmsg、shutdown，以及 pipe/Unix 的 FIONREAD 查询。
  原 Linux 标量、向量、msghdr、地址和控制区预检继续共用，未重写另一套 ABI。
- 阻塞收发交还宿主执行槽，队列变化直接唤醒；只监听共享 pipe/eventfd/Unix stream
  的 epoll 同样直接执行，保留原 ET/ONESHOT 状态和公平游标。
- FD 权限 ledger 的 Rc 保留在原所有者，队列只带 token。触及权限消息时，在消费
  字节或写 guest 输出前返回原处理路径；纯字节前缀可以直接读。带控制区的发送也
  保留原路径，recvmsg 的空控制输出可本地完成。没有返回人为竞态错误或回放部分结果。
- 保留 PEEK、WAITALL、DONTWAIT/O_NONBLOCK、EOF、短收发和 SIGPIPE/NOSIGNAL 边界。
  关闭/半关闭释放字节锁后才通知运行队列。资源快照不增加 guest 端点数；connect 和
  最后关闭失效跨 fork 的 FD/epoll 缓存，避免漏掉连接资格或延迟 EOF/HUP。
- 新增 worker_socket_syscalls，计入已有 worker_direct_syscalls 和 worker_syscalls。
  原生函数继续在同一 worker 执行；本轮未增加原生调用同步或全局原子计数。

## 验证范围

本轮只运行受影响的检查，未重跑完整 CPU、VFS、Linux、网络、GPU、CoreMark、Docker
或 Firefox 验收。Release 严格 Clippy 覆盖 VFS/Linux/Core/CLI/FFI 及 tests，CLI/FFI
Release 构建、fmt、diff 空白检查通过。

| 检查 | 通过数 | 主要内容 |
| --- | ---: | --- |
| VFS ipc | 13 | 新增四项共享队列、并发写记录、FD 权限前缀/回退、连接和 last-close 缓存；既有 datagram/seqpacket、WAITALL、600 层权限链、native Wayland/剪贴板 pipe relay |
| VFS direct_io | 7 | 原共享 pipe/eventfd、跨 fork epoll 失效、ET/ONESHOT 与并发边界 |
| Linux direct | 11 | 新增三项 Unix ABI 对照、非法向量无消费、FIONREAD/WAITALL/NOSIGNAL 和已阻塞 recvmsg 收到权限后的唤醒/回退；原 pipe/eventfd/文件检查复用同一 helper |
| Linux network | 3 | 修改为泛型 CpuMemory 的原地址、向量和预检 helper 的 host socket 用例 |
| Core worker_syscalls | 5 | 不可变快照、checked guest memory、身份/时钟/FD/private futex |
| Core 单槽交接 | 1（六种资源场景） | pipe/eventfd/pipe epoll/Unix read-write/Unix epoll/Unix send-recv；阻塞者让出同一个执行槽，7 条指令只退休一次，预算余 93，无协调服务 |
| Core 旧 Unix 服务 | 1 | 原服务路径的 ready/blocking/nonblocking/SIGPIPE 兼容 |

最初未允许本地 host socket 操作时，native Wayland 回复检查失败，host C oracle 的
shutdown 也被沙箱拒绝。使用该测试所需的本地 socket 执行权限后通过；没有为此修改
产品逻辑。首次 Core 服务检查的模块名过滤到 0 项，已用正确名称补跑实际 1 项。

实现未变化的普通文件存储/OFD、futex、就绪发布与 CPU/MMU 模块复用上轮通过记录；
源码 hash 的复用核对及本轮日志见`worker-unix/results.json`（本地验证记录）。未读取或复制
私人 Firefox 诊断包，没有新的 QEMU、Apple ARM64/16 KiB 页或 iPad 真机验收。

## 真实 musl 与结构对照

新 fixture 使用 ARM64 静态原版 musl，两个 guest 线程交换令牌，同时在原生 NativeCall
中修改并校验同一个 4096 字节 payload。七种模式：read/write、readv/writev、send/recv、
sendmsg/recvmsg、epoll、分片 WAITALL、SCM_RIGHTS。结束时检查半关闭 EOF 和
MSG_NOSIGNAL 的 EPIPE。权限模式每次接收后真实使用传来的 pipe FD，再关闭它。

七种模式 × pinned/spread × 1/2/4 CPU，共 **42 组全部通过**；上轮最终 Release 以
相同 ELF、输入和 host C 断言/输出 oracle 对照，另 **42 组通过**。纯字节模式每个
线程 128 轮，共 256 次原生填充、1 MiB 写入；权限模式各 16 轮，共 32 次原生填充。
原生 fault/reject/fallback 均为 0。单 CPU 沿用 joined 路径；pinned 仅允许 guest CPU 0。

配置四 CPU、pinned 的一次固定工作量结构计数如下。epoch 是全体返回的调度边界，
service 是不结束 epoch 的协调服务，两者必须分别看；send/recv 的旧路径没有 service
请求，也仍会逐次 join。

| 模式 | epoch 前 → 后 | 协调服务完成 前 → 后 | 新增本地 socket 完成数 |
| --- | ---: | ---: | ---: |
| read/write | 286 → 4 | 424 → 0 | 768 |
| readv/writev | 262 → 4 | 508 → 0 | 768 |
| send/recv | 516 → 4 | 0 → 0 | 768 |
| sendmsg/recvmsg | 516 → 4 | 0 → 0 | 768 |
| epoll + read/write | 260 → 4 | 2 → 0 | 768 |
| 分片 WAITALL | 644 → 4 | 0 → 0 | 1024 |
| FD 权限，16 轮 | 100 → 100 | 0 → 0 | 32（查询） |

2/4 CPU 的所有纯字节模式均为 **0 service_requests、0 serviced_syscalls**，epoch
为 3–4。768 是每轮双方各一次查询、接收、发送；WAITALL 的分片发送额外计数。
权限模式依然频繁 join，spread 样本的 epoch 甚至比基线更多，不能宣称已优化权限
传递或完全去掉协调角色。同步令牌协议也不是多核计算扩展性跑分。本轮只解释结构
计数，没有做墙钟统计或推算 Docker/Firefox 的整体收益。

复现（oracle 使用 host 本地 Unix socket/shutdown，运行环境需要允许这些系统操作）：

```sh
bash scripts/build-worker-unix-fixture.sh
cc -O2 -Wall -Wextra -Werror -pthread -DTRAMES_HOST_ORACLE=1 \
  tests/fixtures/worker-unix.c -o target/worker-unix-host
python3 tests/integration/worker-unix.py --runner target/release/trames-run \
  --oracle target/worker-unix-host --output target/worker-unix-results
```

`--baseline /path/to/previous/trames-run` 可增加同工作量结构对照。

## 仍需拆分

Unix bind/listen/connect/accept、FD 权限和表变更、datagram/seqpacket 的 worker 准入、
其他 socket/PTY/timer/signal 资源仍走原所有者路径。native Wayland 的 host relay pump
仍由原调度边界驱动；此轮只共享其 guest 字节队列。文件增长/append、映射/进程
生命周期、GPU 和冻结 cohort 的准备/停止也还保留协调边界。
