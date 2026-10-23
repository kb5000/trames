# 按 inode 共享的 POSIX 文件锁

2026-10-23，第 20 批。结果和源码/产物 SHA256 见
[JSON](worker-record-locks-summary.json)。这是共享内核状态改造的一步，完整 FD 表、
OFD 端点和协调器尚未迁完。

## 实现与边界

POSIX 锁从 Filesystem 内的 owner BTreeMap 拆成实际 inode 对象上的共享 Records。
每个 inode 使用自己的 Mutex 维护范围集合；冲突检查、拆分/合并、额度检查和提交在
同一个事务内完成。不同 inode 的调用不经过共同的资源锁。没有给 VFS/Rc/RefCell
添加 unsafe Send，也没有新增服务线程、消息轮询或普通 CPU/MMU 访存屏障。

- owner 和 worker 共用范围算法与 record_locks.rs 的 flock ABI，包括负长度、
  SEEK_CUR/END、溢出、访问方向、内存错误和 F_GETLK 的写回字段。
- 合格普通文件的 F_GETLK/F_SETLK 在当前 worker 返回，含 EAGAIN 锁冲突。
  它们沿文件控制入口执行，不能把 EAGAIN 交给字节 I/O 的阻塞等待状态机。
- 锁归属使用实际 TGID；fork 不继承锁，pthread 共享进程归属。PID namespace 的
  F_GETLK 可见 PID 使用当前 cohort 的冻结 ID 快照；任务/namespace 变更仍 checkpoint。
- close 继续在原边界执行，但直接释放同一个 inode 锁对象中该进程的全部范围，
  不依赖最后一个 OFD 关闭。另一个进程关闭继承 FD 不释放原进程的锁。
- 进程退出通过弱引用注册表找到锁对象，在注册表解锁后逐 inode 清理；普通锁查询、
  修改和关闭释放不访问注册表。新注册的失效项按增长阈值摊销回收，不逐次全表扫描。
- 锁对象惰性创建；FileEpoch 首次捕获合格文件也会创建一个对象，此后按 inode 复用。
  因此首次文件能力准备有一个小 Arc/Mutex 分配，普通指令和字节 I/O 不增加锁操作。

F_SETLKW 的阻塞唤醒与跨进程死锁检测仍保留原生命周期路径；未准入的 volatile、
下层 overlay、封印等文件也沿原入口。完整 close/dup/FD 复用与端点最后关闭尚未共享，
inotify/epoll、目录/网络控制、VM/task 变更仍需后续改造。

## 受影响验证

107 项不同的 Release Rust 检查通过：VFS 99、Linux 4、core 4，其中 9 项新增。
覆盖范围为锁对象、FileEpoch、FD/clone、存储/挂载/overlay/memfd，以及 seccomp 和
共享/脱离描述符域。核心新增验证包括：

- 两个真实宿主线程争抢同一个写锁 512 轮，每轮恰好一方成功；读锁可以共享。
- 保持一个 inode 的锁后，另一个 inode 可以完成调用。
- 拆分/合并、负长度/溢出、hardlink/unlink、挂载及人为相同数字 inode 的对象隔离。
- retained file 能力与 owner 观察同一集合；关闭本进程任意别名立即释放，关闭其他
  进程的别名不释放；退出按进程归属清理。
- Memory/View 共用 ABI；只读/O_PATH、EFAULT、冲突 PID、未改写的填充字段和 seccomp
  ALLOW/拒绝。未准入的 F_SETLKW 保持原入口。

新增 C fixture 用 CLONE_VM 创建两个不同进程，使用不同 Files 表及共享握手内存。
64 轮交换锁归属，逐次检查 F_GETLK 的父/子 PID、写/读冲突和解锁，合计 576 次操作。
宿主原生及 ARM64 guest 的 1/2/4 CPU 配置全部通过。这里配置 4 CPU 时仍只有两个
测试进程，不能据此推断四核计算扩展性。

原有 m6 fixture 只运行新增的 `locks` 选择入口，复用原来的两组锁用例：阻塞锁、
死锁检测、关闭 dup、拆分、fork，以及 pthread 退出/被杀子进程不错误释放父锁。
1/2/4 CPU 的两组输出均与宿主原生一致；没有重跑 m6 的网络/图形等无关用例。

Release CLI/FFI 构建、五 crate 的 Release all-targets 严格 Clippy、fmt 与差异检查通过。
本轮没有重新运行 CoreMark、Docker、Firefox 或完整兼容性探针；未改动部分复用历史
验证。环境是 Linux x86_64，尚不能替代 Apple ARM64/iPad 的性能与真机验收。

## 同一 ELF 的定向对照

| CPU 配置 | worker 文件锁完成数，前 → 后 | cohort 轮数，前 → 后 | 循环 ms，前 → 后 |
| --- | ---: | ---: | ---: |
| 1 | 0 → 0 | 0 → 0 | 1.262 → 1.239 |
| 2 | 0 → 576 | 577 → 1 | 20.769 → 0.709 |
| 4 | 0 → 576 | 577 → 1 | 20.611 → 0.702 |

前后 service_requests 都为 0；旧路径是结束 cohort 后执行，不能只用服务消息数判断
迁移。这是一轮固定控制密集测试，计时排除进程创建，且受调度和握手影响；这里只
据调用计数、cohort 与结果证明迁移，不推算应用提速倍数。worker_fd_syscalls 在已有
FD 标志控制计数之外，现在也包含准入的 F_GETLK/F_SETLK 成功与错误完成。

复现新增控制对照：先用本机 cc 与 ARM64 musl-gcc 各编译 worker-record-locks.c，再运行：

```sh
python3 tests/integration/worker-fd-status.py --kind locks \
  --runner target/perf-runtime/release/trames-run \
  --baseline target/worker-record-locks-checks/before-trames-run \
  --oracle target/worker-record-locks-checks/native-record-locks \
  --output target/worker-record-locks-checks/recheck
```

m6 的选择入口为 `m6 locks`。原始输出在 target/worker-record-locks-checks，兼容清单
仅更新 syscall 25 的说明和证据，318 项状态基线保持不变。
