# 阻塞 flock 的共享通知等待

2026-10-23，第 26 批。阻塞 flock 在合格普通文件上由当前 worker 执行，冲突时交还
执行槽，inode 解锁直接通知原任务。没有新增线程、消息轮询或协调 RPC。

## 实现与生命周期

- `FlockRequest` 保留原始 OFD 的锁租约和操作；先尝试，发生冲突才订阅通知，订阅后
  再检查。唤醒继续已退休的 SVC，不重复退休，不重新解析数字 FD。
- cohort 停止时 `IoCall` 保留请求。所有 worker 返回后、任何冷 close/dup 执行前，
  Process 保留完整 OFD。FD 关闭重用后仍等待原文件；同 OFD 连续执行不同类型的等待
  时复用文件引用、更新请求类型，不能误用上一次的 SH/EX。
- caught signal 清除等待；EINTR/SA_RESTART 复用既有信号语义。SIGSTOP 保留等待，
  成功 exec/退出清理。共享锁转独占锁失败时旧锁已释放，不能退回 owner 重放转换。
- 同 inode 的等待者可以与单个活跃任务共享执行批次；无关等待者不改变独立计算入口。
  单 CPU 无竞争调用直接完成，仅在确实阻塞时保留额外请求/文件引用。
- 删除 Kernel 的旧数字 FD flock 重试分支，owner 与 worker 共用 inode 锁算法。
  NB 和 unlock 不进入等待。权限过滤在接受新 worker 调用前执行。

完整 OFD/Node 和 FD 表结构仍在 owner，跨批次保留引用的最终析构仍在 join。
所以 close/dup/exec/映射和整体 cohort 的协调角色还在。本批不宣称完整去协调器，
也不把 `service_requests=0` 当作完成判据。

## 验证

按受影响范围累计 **92 个不同 Rust 测试通过**（core 16、Linux 71、VFS 5），
包括新增单执行槽等待/解锁/恢复、SVC 恰好退休一次、stop 交接释放旧订阅、
FD 重用、连续 SH→EX 请求、信号中断/重启/停止/退出、seccomp；以及受 PendingIo
字段变更影响的 byte/timer/io_uring 和运行队列检查。已通过且实现未变的结果复用，
最后两处改动仅复查 owner/交接相关项目。没有运行完整 CPU/GPU/浏览器/Docker 套件。

`blocking-flock.c` 同时在宿主 Linux 原生运行和静态 ARM64 guest 上运行。
内存与磁盘后端各验证 1/2/4 CPU：阻塞 FD 被关闭并重用、EINTR、SA_RESTART，
以及 64 轮竞争（256 次锁控制）。旧 runner 可复现 FD 重用后错误提前完成，
最终实现全部通过。测试的让出/等待逻辑只用于构造竞争，生产等待采用通知。

Release CLI/FFI、五 crate/all-targets 严格 Clippy、格式和差异检查通过。
最终交接修正后重建，定向单测与 2/4 CPU 两种后端再通过；未改变的单 CPU 和 worker
基准结果沿用相应已验证产物，逐项哈希与复用说明见 JSON。

## 小负载对照

Linux x86_64 宿主，同一静态 ARM64 ELF，修改前后交错各 3 次，耗时中位数：

| Guest CPU | 修改前 | 修改后 | 协调批次 |
| --- | ---: | ---: | --- |
| 1 | 0.527 ms | 0.536 ms | 0 → 0 |
| 2 | 4.509 ms | 0.750 ms | 131–133 → 3–4 |
| 4 | 5.632 ms | 1.220 ms | 131–132 → 3 |

双核/四核 256 次控制全部在 worker 完成，产生 63–64 次通知等待；前后 service
request 都是 0，改进体现在减少全批次返回。单核未测得收益；这是短锁竞争负载，
样本受宿主调度及测试同步影响，不能外推为应用整体、iPad、CoreMark 或 Docker 的加速比。

- 汇总与源码/产物哈希：[`blocking-flock-summary.json`](blocking-flock-summary.json)
- 可复用 guest/native 对照入口：[`blocking-flock.py`](../../tests/integration/blocking-flock.py)
- 本地原始记录：`target/blocking-flock-checks/`

本轮未提交、推送或生成 IPA。
