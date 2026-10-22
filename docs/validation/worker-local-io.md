# 拆分协调器：第一批 worker 本地共享资源

> 本文为公开的验证汇总。提及的原始日志、二进制和逐项 JSON 记录保留在开发环境，未随本次源码提交公开。

2026-10-22，沿[三组通信对照](coordination-cost.md)开始生产迁移。当前只完成资源层的
第一批拆分，**协调线程仍然存在**。没有引入轮询消息总线或额外服务线程，没有构建 IPA。

## 已完成的生产路径

- eventfd 状态通过 Arc 共享；计数用原子 RMW，成功写 release、成功读 acquire，
  保留实际通知对先前数据的发布关系。readiness 版本用 relaxed 原子；普通 guest
  读写没有新增屏障或锁。semaphore、计数上限与 errno 保持有效。
- 匿名管道共享原始数据队列，Mutex 只覆盖这一条管道的 bytes、端点数和 readiness
  不变量。直接 I/O、原 VFS、poll、splice、FIFO 与 native pipe relay 使用同一状态。
  原有 EOF、PIPE_BUF 原子写、短写、背压和字节顺序算法只有一个实现。
- read/write/readv/writev 在当前 worker 完成，复用 Linux 的 guest 访存预检和
  复制 helper，不传递 Process / Linux / VFS 的 Rc/RefCell，不移动 CPU、寄存器或
  内存 View，也不发送成功调用的请求/回复。原生计算继续留在同一个 worker。
- 阻塞 EAGAIN 与 SIGPIPE 在无效果时直接回调度器，省掉一次失败 RPC；非阻塞错误
  与已完成的部分操作直接返回。不能因 stop 或后续记录失败重放已经完成的操作。
- FD flags 与共享句柄同一次扫描生成并缓存，暖 dispatch 复用 Arc。FD 关闭/重用、
  dup、CLOEXEC 与 fork 后共享 status flags 的更新使缓存失效。写入 scratch 在持久
  worker 上复用、大小受原 1 MiB transfer cap 约束，不在每个 epoch 重新分配。
- CLI worker_direct_syscalls 单列这一批调用，并计入 worker_syscalls；由 worker
  私有 Cell 计数，完成 dispatch 后汇总，没有新增逐调用共享统计 RMW。

host C oracle 同时暴露了原有 eventfd 向量兼容问题：不能将 3+5 字节 iovec 拼成一条
8 字节 eventfd 写入。原路径与新路径现在都按各 iovec 的记录长度执行，两个 8 字节
写入返回 16；后续记录失败保留先前成功字节数。普通 pipe 的 writev 继续先收集成一次
写入，保留 PIPE_BUF 原子性。没有把所有 byte stream 都改成逐 iovec 操作。

## 验证结果与范围

Linux x86_64 Release，原版静态 musl ARM64 fixture 与独立本机 C 输出 oracle。
同一 ELF 的 pipe / eventfd 各运行 1/2/4 CPU 配置，共 6 组；每组是 **2 个 guest
线程**，不是配置 4 CPU 就宣称有 4 个活跃线程。内存线程固定完成 128 次 1 MiB 原生
填充，I/O 线程固定完成 1024 轮数据操作，并穿插 mmap/munmap、dup/close。

| 模式 | 配置 CPU | worker 本地 I/O | 协调 RPC |
| --- | ---: | ---: | ---: |
| pipe | 1 | 0 | 0 |
| pipe | 2 | 2048 | 0 |
| pipe | 4 | 2048 | 0 |
| eventfd | 1 | 0 | 0 |
| eventfd | 2 | 2050 | 0 |
| eventfd | 4 | 2050 | 0 |

eventfd 多两次是记录长度 EINVAL 与空计数 EAGAIN 的负例；负例不改变资源状态。
六组输出与 host oracle 完全一致；原生函数/字节数正确，fault/reject/fallback 均为零。
单 CPU 原有路径本来就在当前执行线程完成 I/O；只剩一个 ready 任务时也可以使用它，
所以一般不要求 worker 计数等于全部 I/O 数量。该表是功能和路径验收，不是提速数据。

Core 全范围 101 项通过、2 项手工 benchmark 忽略；后续 worker buffer 的所有权移交
只补受影响的 19 项 worker 测试。VFS 177 项通过、1 项既有忽略；合并 FD 快照缓存后
只补 4 项共享资源/cache 测试和 1 项 worker FD flags 测试。Linux 32 项范围覆盖完成，
记录/向量语义修正与最后的快照变更补查 direct 用例。五个受影响 crate 的 tests 严格
Clippy 通过，Release CLI/FFI 构建通过。源码未变的 CPU/MMU/GPU/UI 通过记录复用。

部分本地 socket 测试最初在沙箱中返回 EPERM；明确授予本地测试网络访问后，只重试
5 项 VFS 和 3 项 Linux 失败用例并通过，未因此重跑其余已通过测试。
早期全范围日志与后续精确补查分别保留，不混成一次完整运行。

确定性用例覆盖并发 eventfd 生产者/semaphore 消费、计数溢出、并发管道记录完整性、
不同管道锁互不阻塞、PIPE_BUF 背压、retained handles 不阻止 EOF、fork flags 与 FD
重用、标量/向量 EFAULT 不消费输入、短返回、seccomp ALLOW/拒绝及精确 SVC 预算。
worker 测试直接观察 Done 事件，确认就绪管道读取未请求服务，阻塞读取也没有先发
失败 RPC。不是仅用最终程序输出推断是否经过协调器。

`worker-local-io/results.json`（本地验证记录）与同目录 stdout/stderr 保存
最终 fixture 数据。没有新的应用性能对照，没有 Apple ARM64/16 KiB 或真机验收。
此前通信微基准保持历史证据，不因本轮迁移重跑。

## 剩余集中路径与继续拆分的边界

当前 FD 变更仍是 joined 操作。DirectFd 是这个边界内的保留资源句柄，不是已经完成的
并发 open-description 生命周期；并发 close/reuse、flags 修改还需共享 FD 表与 OFD
租约。不能直接删掉现有 join，让旧 FD 快照在新一轮权限状态下继续使用。

阻塞任务/futex/epoll、进程生命周期、映射和 backing 提交、其余 VFS/网络服务与
stateful Vulkan 仍保留旧协调路径；交互会话的周期全体检查点也还存在。
下一步按以下依赖继续，生产默认选择共享状态和必要的对象级同步：

1. 拆 task 的执行状态、就绪/等待队列、唤醒与定时器；阻塞的是 guest task，不能
   占着 host worker 等回复。没有工作时使用事件/条件变量休眠，不做忙等消息通信。
2. futex 使用地址/共享 backing 键范围的 compare/enqueue/wake 同步，避免每次触发
   全体暂停；保留正常 ARM 竞态结果，不给整个 guest 程序增加锁。
3. 映射修改、fork/exec、退出按 VM/线程组缩小暂停范围；明确 View、私有页快速路径、
   TLB/code token 与旧页寿命的失效协议，不能只保留旧内存而忽略权限撤销。
4. 将其余 FD/OFD、文件 backing、网络与 GPU 对象逐项拆成共享资源；复用原算法，
   避免用一个全局 Kernel/VFS mutex 替代现在的协调线程。
5. 执行线程接管剩余低频控制工作，撤掉专用协调角色。UI 主线程及必要的异步宿主
   I/O 完成机制保留其原职责；原生计算不为适配异步结构而交给别的线程执行。

复现入口（先完成构建，输出目录不得存在）：

```sh
bash scripts/build-epoch-services-fixture.sh
cc -O2 -Wall -Wextra -Werror -pthread -DTRAMES_HOST_ORACLE=1 \
  tests/fixtures/epoch-services.c -o target/epoch-services-host
cargo build -p trames-cli -p trames-ffi --release
python3 tests/integration/worker-local-io.py \
  --runner target/release/trames-run --oracle target/epoch-services-host \
  --output target/worker-local-check
```
