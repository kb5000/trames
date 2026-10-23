# 共享 futex 与 requeue 的工作线程路径

2026-10-22，第十一批协调器拆分。普通 private/shared futex WAIT/WAKE、位集与
REQUEUE/CMP_REQUEUE 使用现有共享队列和原任务；所有者与 worker 共用 ABI helper。
设计及保留边界见[架构说明](../architecture/epoch-services.md)。

## 检查

按改动范围完成 34 项不同单元测试及 release Clippy、CLI/FFI 构建、格式/脚本检查。
覆盖共享映射别名、不同 VM、fork、tagged 地址、增量 remap/unmap/protect、稀疏索引、
比较失败不产生效果、错误先后顺序、seccomp ALLOW/拒绝、位集、取消/超时、反向并发
requeue 和移除旧 ticket。一个宿主槽完成跨 VM 的 WAIT→CMP_REQUEUE→WAKE，精确退休
13 条指令且没有服务请求，证明等待者可以让出执行槽后在原队列恢复。

先通过的 14 项 futex 测试直接复用；新增汇编单测修正 syscall 返回值覆盖 x0 后，只
重跑该失败项。其余受影响检查为 ABI 1、队列 5、private handoff 1、共享内存 7、增量
快照 5。没有重复 CPU、网络、GPU、浏览器或 Docker 全套测试。

## 原版 musl 程序对照

同一静态 ARM64 musl ELF，两个持久等待线程、64 轮固定 futex 操作，并保留一个调用
原生 memset 的 peer。private/shared 两种模式均在修改前后 1/2/4 CPU 完成，与本机 C
输出参考一致。共享模式将同一 memfd 映射到两个不同 VA；测试验证跨别名等待/迁移、
CMP_REQUEUE 比较失败回滚、一次操作唤醒一个并移动另一个，以及迁移后原位集。
12 次对照运行全部通过，服务请求为零；多 CPU 新路径至少完成 128 次本地 requeue。

| 模式 | CPU | 修改前整组返回次数 | 修改后 |
| --- | ---: | ---: | ---: |
| private | 2 | 213 | 22 |
| private | 4 | 2393 | 22 |
| shared | 2 | 787 | 21 |
| shared | 4 | 925 | 21 |

单 CPU 前后均不使用并行 cohort。另复用未修改的旧 musl futex ELF，1/2/4 CPU 的
private 位集/requeue、超时/错误及共享匿名映射 fork 唤醒全部通过，共 3 次运行。

以上是 Linux x86_64 宿主的单次结构样本。原生 peer 持续工作到主流程结束，迭代次数
随调度变化，不能把墙钟时间或返回次数比例当固定工作量跑分，更不能当 Docker/iPad
加速比。Apple ARM64/16 KiB 与真机验收尚未执行。普通指令访存没有新增检查、屏障或
per-page 字段；共享 backing key 仅在映射准备阶段建立稀疏索引。

汇总、源码及二进制哈希见 [worker-futex-summary.json](worker-futex-summary.json)。原始
开发日志和逐项报告保留在 target/worker-futex-checks，未随本次摘要公开。
