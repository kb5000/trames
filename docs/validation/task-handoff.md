# 共享运行队列、worker 阻塞与继续执行

> 本文为公开的验证汇总。提及的原始日志、二进制和逐项 JSON 记录保留在开发环境，未随本次源码提交公开。

2026-10-22，接续[共享 futex](shared-futex.md)，本轮将 private futex 的实际等待与就绪
任务交接移到 worker。**协调线程尚未完全删除**，未发布 IPA。源码为包含前两批改动
的本地工作树，记录不代表已推送 GitHub。

## 本轮范围

- 同一解释器循环从共享 ready 队列取任务。private WAIT/WAIT_BITSET 比较并入队后
  释放执行槽；WAKE/WAKE_BITSET 直接重新排队。sched_yield 直接让出，其他 CPU 和
  原生函数继续执行，不通过协调 RPC 或全体暂停。WAKE 找到 cohort 外的等待者时
  仍请求旧边界，避免丢失外部任务通知。
- 所有 ready 任务及同 VM 的未冻结 futex 等待者组成执行 cohort，View 在启动前一次
  准备并准入。任务保留原 View，不复制 guest 内存；CPU、TLB/翻译缓存和快照固定在
  同一个 Box 分配里，调度移动指针。普通解释/原生运算不持调度锁。
- 等待注册使用 Arc，队列/位置分别同步，状态仍用 relaxed CAS。比较与入队共享
  futex 桶锁，通知运行队列前释放桶锁；wake-before-park、超时/唤醒、取消与 PID 重用
  都保留唯一结果。空闲 worker 用条件变量及实际 deadline 休眠，无轮询总线或新线程。
- affinity 首次槽位沿用原匹配结果，防止 pinned 任务被 flexible 任务饿死；迁移更新
  getcpu 并清除 exclusive reservation。每任务累计预算、decode/native/syscall 指标，
  每 CPU 用时按实际执行槽累计；SVC 退休一次，未使用 credit 在让出时退还。
- WAIT 的 bitset/相对、绝对与 realtime 超时解析与 joined 路径共用 helper。trace、
  非 ALLOW seccomp、共享 key 解析、REQUEUE、生命周期/映射/FD/权限变更继续 join。
- 结束时先停止并收齐全部执行槽，收完已有磁盘作业，再释放 ready/parked 中的 View、
  提交 backing，最后执行冷路径。请求意外丢弃时其 View 已经释放，故障返回不再要求
  取回该 View。没有把 Kernel/VFS 的 Rc/RefCell 标记为 Send。

## 固定工作量对照

Linux x86_64、Release，同一静态 ARM64 musl ELF。两个 guest 线程做 2048 轮交替
接力，共 4096 次原生 4 KiB 填充（16 MiB），校验发布数据、固定 checksum 与独立 host
C oracle。前版本是本轮开始前保留的二进制（已含上一批共享 futex），散列见原始数据。

| 配置 CPU | 改前耗时 | 改后耗时 | 改前全局 epoch | 改后全局 epoch |
| --- | ---: | ---: | ---: | ---: |
| 1 | 5.753 ms | 5.774 ms | 0 | 0 |
| 2 | 51.284 ms | 25.138 ms | 2051 | 5 |
| 4 | 49.606 ms | 25.477 ms | 2054 | 5 |

这是隔离运行时每组一次采样，只能说明该接力场景约 2 倍的本机观测；不是应用、Docker、
CoreMark 或 iPad 提速结论。两线程互相等待，本身不具备四线程持续并行工作量，不能
用本表衡量四核计算扩展性。首次与功能检查重叠的计时已弃用，没有混入最终表。
多核改后在 worker 上完成约 4090 次 WAIT/重新就绪，协调 RPC 均为 0。最终 join 等待
可能由线程退出后的 clear_tid 在旧边界唤醒，因此 waits 与 worker resumes 不强求相等。

## 正确性与构建

- 最终 core 116 项通过，2 项既有手工基准忽略；严格 Clippy（core/CLI/FFI，tests，
  Release，`-D warnings`）及 Release CLI/FFI 构建通过。首次全范围发现的异常请求
  View 归还失败已修正，最终结果全部通过。没有触发完整工作区或图形验收。
- 确定性 worker 用例在**只有一个宿主 worker** 时运行两个 guest 任务，完成 WAIT →
  写数据/WAKE → yield → 原任务继续执行；只收到最终 EpochStopped，没有 Service。
  恰好 9 条指令、3 次执行槽分配，剩余预算正确，内存提交后得到发布值 42。
- 队列用例覆盖 wake-before-park、取消归还等待任务、超时后在允许的 CPU 继续、
  stop 唤醒空闲 worker、首次 pinned/flexible 槽位，以及原有 futex 竞争与生命周期。
- 真实 musl futex bitset/requeue/超时/shared-fork 的 1/2/4 CPU 三组通过；pipe/eventfd
  的 1/2/4 CPU 六组通过，原生 fault/reject/fallback 为 0，RPC 为 0。
- 原有完整 SMP fixture 在新版本的 1/2/4 CPU 通过：topology/affinity、LSE/LLSC/CASP、
  跨页 acquire/release、DMB、futex、并发 fork COW/shared atomics、exec/exit_group。
  本机未安装 QEMU，复用既有 CPU oracle，额外与保留的旧版单 CPU 输出对照；没有
  宣称执行了新的 CPU 差分 oracle。
- 原有动态 musl native-parallel 的 2/4 CPU、4 CPU shared-stack 与磁盘四组通过，19
  个原生函数全部被调用，无 function fallback、fault、reject 或 checkpoint retry。
  原 fixture 缺失时只重编译了这一项；期望输出复用已有 QEMU/host 验证记录。
- 未修改 CPU/MMU/VFS/Linux/网络/GPU/UI 的实现，复用其已有单测；新执行所有权涉及的
  原生与 SMP 集成另外补查。没有重跑 CoreMark、Docker、Firefox 或旧通信微基准。

`task-handoff/results.json`（本地验证记录）记录最终产物与上述检查。
没有 Apple ARM64、16 KiB 页或 iPad 真机验收，本轮不构建 IPA。

## 剩余边界

当前仍是冻结 cohort：公平性/外部事件的周期检查、映射/进程/权限变化结束整轮；
尚未实现按 VM 单独撤销租约，也不在活跃 cohort 内创建新 View。大量长期等待者的
View 准备会增加元数据成本，后面可用更细的 VM 租约减少预留。

普通文件/网络/PTY/GPU 的对象与生命周期还需继续共享化；原来的 FD 服务请求仍会
占用调用者宿主槽等待回复，不能把本轮 futex 的执行槽释放泛化成所有 syscall 已异步。
下一批可迁移阻塞 pipe/eventfd/epoll 与共享 key futex，再处理文件服务完成时的任务接手，
最后缩小映射/生命周期的暂停范围并移除剩余专用协调角色。

复现固定工作量对照（保留改前二进制，输出目录不得存在）：

```sh
bash scripts/build-task-handoff-fixture.sh
cc -O2 -Wall -Wextra -Werror -pthread -DTRAMES_HOST_ORACLE=1 \
  tests/fixtures/task-handoff.c -o target/task-handoff-host
cargo build -p trames-cli -p trames-ffi --release
python3 tests/performance/task-handoff.py --baseline /path/to/trames-run-before \
  --runner target/release/trames-run --oracle target/task-handoff-host \
  --output target/task-handoff-comparison
```
