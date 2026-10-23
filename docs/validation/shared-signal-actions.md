# 共享 sighand 与 worker 信号表查询

2026-10-23，第 23 批。接在共享 FS 上下文之后，继续移除协调器的状态复制。

## 变更

- CLONE_SIGHAND 任务持有同一个处理函数表；fork 复制，成功 exec 私有化并重置捕获
  的处理函数、保留 SIG_IGN。失败 exec 保留原域，SA_RESETHAND 直接修改共享记录。
  删除 actions_id、逐任务数组传播和 ResourceChanges；剩余 heap 同步单独命名保留。
- 多字段 Action 使用 sighand 对象级锁，读取一次取出完整记录；查询随后在锁外写
  guest 内存。owner/worker 共用 ABI、参数/地址检查、标志及信号 mask 处理，避免
  两套 sigaction 实现。没有新服务线程、轮询总线或普通 guest 内存屏障。
- 仅 act=NULL 的 rt_sigaction 查询进入 worker，并先检查 seccomp。修改仍在原任务
  入口处理 pending 丢弃和作业控制副作用；不把表共享误当成整个信号系统已迁出。

FD/OFD、路径/namespace、VM/task 生命周期以及 heap 传播仍待迁移；信号 pending/mask
尚未共享到 worker。现有 worker 的终端作业控制快照也尚未改为动态信号状态。

## 验证

134 项不同的受影响 Release core 检查通过，覆盖共享/复制/exec 域、信号帧与同步
故障、SA_RESETHAND、seccomp、并发完整记录交换、worker 等待和生命周期。
修复新增 seccomp 检查发现的查询准入遗漏后，只重跑受影响的过滤检查；其他通过项
复用。本轮另修正旧 timer 测试的过期计数预期：epoll_wait 已与 ppoll 共用 poll 完成
计数，生产 timer/poll 代码未改变。

宿主原生与真实 ARM64 guest fixture 在内存/磁盘后端各通过 1/2/4 CPU：独立 fork、
共享 VM 但私有 sighand、CLONE_SIGHAND、实际信号投递/重置、成功和失败 exec。
原始 CLONE_VM child 复用父任务的 musl TLS，fixture 用原始 getpid/gettid/tgkill
定位当前任务，避免 raise 使用缓存的父 TID；该调整不改变产品信号实现。

Release CLI/FFI、三个受影响 crate 的 all-targets 严格 Clippy、格式与差异检查通过。
未重复完整 CPU、网络、图形、Docker/Firefox 或 CoreMark 验收。

## 定向对照

同一静态 ARM64 ELF、两个线程共 512 次 rt_sigaction 查询。两版使用相同 benchmark
模式，每个 CPU 配置交错 3 次；完整域语义另行验证。

| Guest CPU | 改前中位数 | 改后中位数 | 并行批次（完整基准程序） |
| --- | ---: | ---: | --- |
| 1 | 0.558 ms | 0.536 ms | 0 → 0 |
| 2 | 14.026 ms | 0.292 ms | 513–515 → 3–4 |
| 4 | 13.231 ms | 0.312 ms | 511–515 → 3 |

两版 service_requests 都为 0；减少的是整组返回与重建。worker_syscalls 还包含
yield/时钟等调用，不能当成精确 sigaction 计数。这个很短的查询密集测试运行于
Linux x86_64 宿主，不代表 iPad、CoreMark 或应用总体加速比。

证据和哈希：[shared-signal-actions-summary.json](shared-signal-actions-summary.json)。
原始产物：`target/shared-signal-actions-checks/`。本次未提交、发布或构建 IPA。
