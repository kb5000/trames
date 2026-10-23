# 共享系统日志与 worker syslog

2026-10-23，第 36 批。机器记录见
[shared-kernel-log-summary.json](shared-kernel-log-summary.json)。

会话日志从 Rc/RefCell 改为独立 Arc 资源，VFS、打开的 kmsg 描述符和 worker
引用同一份记录、清空位置及 legacy stream 游标。Mutex 只保护有限大小的日志状态；
读取快照并清空在同一个事务完成，输出 guest 内存时不持有日志锁。保留原来的
256 KiB 容量、消息转义、速率限制、独立 kmsg 游标与清空语义。

syslog 的非阻塞操作现在由当前 worker 执行，包括读取快照、读取并清空、清空、
查询未读量和容量。复用一个 Linux ABI 实现，不复制协议或改变错误顺序。
权限能力按冻结的 worker epoch 捕获，凭据/namespace 修改仍先 join；seccomp
ALLOW 才执行，其他动作沿原过滤路径。消费或清空前仍校验完整输出区间，防止
EFAULT 已经改变日志。共享引用可以在 VFS 销毁后由 worker 使用和最后释放。

SYSLOG_ACTION_READ（action 2）会阻塞，仍使用原 task 等待路径，不从 worker
直接返回 EAGAIN。`/dev/kmsg` 的 FD 游标、读写及 poll 也仍由原 OFD 所有者操作。
本批移除日志的 Rc 依赖和非阻塞 syslog 往返，没有宣称这些等待或完整 OFD 已迁出。
未增加服务线程、轮询、CPU/native 普通访存检查或逐次访存锁。

上述为第 36 批边界；第 37 批已继续迁移阻塞读取和 kmsg 游标/I/O，见
[日志等待](shared-log-waits.md)。

## 验证和路由对照

- 8 项定向 release Rust 检查通过：并发 append/READ_CLEAR 无丢失重复、独立读取
  与溢出限流、worker 最后引用与 epoch 权限、输出故障不消费/清空、seccomp 和
  原有子进程非法指令日志。
- 新 ARM64 guest fixture 在内存/磁盘 × 1/2/4 CPU 共 6 组通过。覆盖双线程查询、
  fork 后降权、seccomp 拒绝、非法参数、READ_CLEAR、legacy stream 和 kmsg。
  同一 ELF 在改动前 runner 的内存 × 1/2/4 CPU 共 3 组通过。
- 原 compatibility-log fixture 只运行日志子集：内存 2 CPU、磁盘 4 CPU 共 2 组。
  验证子进程 ENOSYS/SIGILL、阻塞 kmsg 跟随、exec 后实际 Alpine BusyBox dmesg。
  BusyBox 和 musl 从已有 APK 提取，没有重跑不受影响的文件 ABI 子集。
- Release CLI/FFI、严格 release Clippy、格式与差异空白检查通过。

同一双线程 fixture 的 768 次查询，内存后端观察到：

| Guest CPU | 改动前 parallel_rounds | 改动后 parallel_rounds | 改动后 worker_syscalls | service_requests |
| --- | ---: | ---: | ---: | ---: |
| 1 | 0 | 0 | 0 | 0 |
| 2 | 406 | 10 | 772 | 0 |
| 4 | 406 | 10 | 772 | 0 |

worker_syscalls 包含 fixture 的其他 worker 调用。线程结束顺序会影响路由，先结束
的线程可能让另一个线程进入单活跃解释器路径；磁盘 2/4 CPU 分别观察到 629/770
次 worker 调用和 15/15 个并行批次。单活跃路径无需协调 worker，不应强制拉回
并行执行。此处是一次固定工作量的路由对照，不是耗时基准或整体加速比。

验证期间修正两项测试假设：初版 fixture 对空 legacy stream 期待 EAGAIN，但原
调度器会等待，因此改为只在查询未读量非零时读取；初版路由断言要求全部 768 次
查询都计入 worker，忽略了单活跃路径。后者复用已经通过的 guest 输出重新核对
路由，只补跑尚未执行的磁盘 4 CPU 项，没有重复通过项目。源码审查同时将 action 2
排除出 worker 直接返回路径，核心测试明确检查该边界。

原始日志和 ELF 留在本地 `target/shared-kernel-log-checks/`。宿主内核日志不作为
测试对象；未做 CoreMark、Docker、浏览器或图形复测，未制作 IPA，也没有 Apple/iPad
性能结论。完整 FD/OFD、Node/映射页、VM/task 生命周期和全体暂停仍需继续拆分。
