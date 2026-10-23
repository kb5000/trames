# 共享 OFD 状态与 worker FD 控制

2026-10-23，第 18 批协调依赖迁移。结果见 [JSON](worker-fd-status-summary.json)。
尚未进入 IPA；FD 表结构和完整 OFD 所有权仍未共享完毕。

## 改动

此前 O_NONBLOCK/O_APPEND 与其他打开标志存放在 owner 的 Rc/RefCell 中，worker 只有
冻结数值。F_SETFL/FIONBIO 会交回 owner，状态变化通过全局 revision 使独立 fork
FD 表的能力缓存失效；高频小操作因此反复结束整个 cohort。

现在不可变访问/打开属性与可变状态分开：后者由同一 OFD 的 Arc<AtomicU32> 持有，
owner、worker、dup/fork、SCM_RIGHTS 共用。F_SETFL 验证原有 flag 子集后，一次 relaxed
store 替换两个可变位，不需要 CAS 或资源锁。FIONBIO 使用原子位操作，只改 NONBLOCK，
保留 APPEND。没有发布 guest 数据/FD 表或权限，故不引入 acquire/release 屏障。
原来的全局 revision 仍用于资源最后关闭等生命周期失效，status 设置不再修改它。

FdFlags 冻结 FD 表成员/CLOEXEC，包含活的 OFD 状态，不再是所有字段的数值快照。
pipe/eventfd/timerfd/Unix/PTY 的 nonblocking、原生 TCP 的阻塞判断，以及普通文件的
append 准入读取同一状态。文件增长、append、同步/持久化仍在产生效果前回到原服务。

字节调用已经 park 后，下次重试 EAGAIN 也会读取最新 NONBLOCK；设置标志本身不会
制造资源唤醒。poll/epoll 不因文件标志 NONBLOCK 而提前返回 EAGAIN。
seccomp 在实际状态修改前验证，拒绝/trace 仍保留原内核路径；EBADF/O_PATH/EFAULT
和无效 flag 的原有边界保留。CLOEXEC/dup/close 等表项操作尚未移出原路径。

每个原 OFD 新增一份小 Arc 原子存储，捕获能力克隆该存储；它不保活实际 pipe 端点或
宿主 socket。不可变的直接 I/O 访问位继续是 plain value，额外活状态读取只用于实际
查询、非阻塞/append 判断；普通 CPU/MMU 访存不增加检查、锁或屏障。所有状态设置
共用 fd_status.rs 的算法，没有复制一份 worker 文件/管道实现。

## 正确性与构建

141 个不同的定向 Rust 检查通过：VFS 80、普通 worker 字节 I/O 17、io_uring 40、
owner FIONBIO 1、core FD/seccomp 3。8 项为本轮新增回归，其余覆盖受影响的已有语义。
最终 EAGAIN 冷路径修复后重跑 17 项普通字节检查；不受影响的 40 项 io_uring 结果复用。
VFS 范围包含共享状态、FD 复用/最后关闭、dup/fork/CLOEXEC、文件 offset/append、IPC、
PTY、映射权限、网络和事件资源；没有重跑整个 CPU/图形/Firefox/Docker 验收。

Release CLI/FFI 构建和 VFS/Linux/core/CLI/FFI 五 crate 的 Release all-targets 严格
Clippy 通过；fmt、Python AST、数据与差异空白检查通过。

同一 C fixture 在宿主原生运行作为结果参照；ARM64 ELF 在 1/2/4 CPU 前后均通过。
每个双核/四核运行精确完成 1280 次 worker FD 控制，256 次空管道 read 留在 worker，
service_requests 为 0。新计数 worker_fd_syscalls 包含 F_GETFD/F_GETFL/F_SETFL/FIONBIO
的完成（含错误），不代表所有 FD 系统调用已迁移。

最终冷重试修复后又只检查 2/4 CPU worker 用例；单 CPU owner 结果复用。同一 ELF
保证 O_NONBLOCK 在 read 开始前已打开，不会进入最后修改的 pending-read 冷路径，
故下述控制密集基准保留原始 runner SHA 和数据，没有伪装成最终 runner 重测。

## 固定工作量对照

宿主 Linux x86_64，Release，无 JIT。一个 guest 线程保持可运行，主线程执行 256 轮，
每轮 5 次 FD 控制和 1 次空 pipe read。2/4 CPU 各 5 对样本，交替前后运行顺序；
记录阶段无并发构建。计时只含主线程循环，原始 host CPU/墙钟也保留在 JSON。

| CPU | 修改前循环中位数 ms | 修改后循环中位数 ms | 修改前 cohort 轮数 | 修改后 cohort 轮数 |
| --- | ---: | ---: | ---: | ---: |
| 2 | 239.663 | 0.713 | 771–772 | 3–4 |
| 4 | 221.638 | 0.721 | 771–772 | 3–4 |

这是专门隔离协调开销的工作量；单次调用很短，反复拆/建 cohort 的成本占比极高。
修改前四核样本约 30–242 ms，调度波动明显，不以表中比值代表正常程序的提速。
稳健的结构证据是同一 1280 次操作不再离开 worker，cohort 从约 771/772 次降到
3/4 次。它不能替代 iPad/kqueue、CoreMark 或 Docker 的端到端测量。
初次单 CPU 的相同循环约 123/122 ms，是 guest 双线程的串行调度，不用于衡量
单线程解释器效率；不能据此声称 CPU/MMU 有大幅提速。

## 剩余边界

FD 创建/安装/关闭/dup/CLOEXEC、完整 OFD 本体、namespace 权限和映射/进程生命周期
仍可能要求 owner/cohort 返回。下一步需要让实际 FD 表和资源最后关闭具备共享所有权，
保留 close/reuse、代际、CLOEXEC 和 endpoint/FIN 语义，再把暂停范围缩到相关 VM/表。
此次没有把 VFS 放进全局锁，没有发送 Rc/RefCell，也没有增加协调/服务线程。

原始检查在 target/worker-fd-status-checks；源码、最终/基准 runner 和 ELF SHA256
在 JSON 中。兼容清单只更新 25/29 的说明，不重复执行 318 项 syscall probe。
