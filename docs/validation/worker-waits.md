# timerfd 与 ppoll 的局部等待

> 本文为公开的验证汇总。提及的原始日志、二进制和逐项 JSON 记录保留在开发环境，未随本次源码提交公开。

2026-10-22，第八批协调成本拆分，接续 [Unix stream worker 路径](worker-unix.md)。
本轮仅修改本地工作树；实现边界见[架构记录](../architecture/epoch-services.md#timerfdppoll-与局部计时调度)。

## 实现范围

- timerfd 的计时和 pending 状态由 VFS 与 worker 共用一个 Arc 对象；对象锁只保护同一个
  timer 的多字段不变量。复用原到期算法和 Linux get/set ABI，输出预检后再修改状态。
  read、gettime、settime、混合共享资源的 epoll 均可直接完成，支持原 ET/ONESHOT。
- ppoll 在无临时 signal mask、仅包含 pipe/eventfd/timerfd/已连接 Unix stream 时直接
  执行；支持负 FD、空数组超时和完整输出预检。未知/特殊 FD 在产生效果前退回原路径。
  实际阻塞保留准备好的数组、View 和原任务，资源通知/到期只恢复它，不重放 SVC。
- 分开记录 syscall timeout 和资源 deadline。timer rearm/disarm 直接通知等待者重新
  计算资源时间；不会延长原调用超时。关闭/跨 fork epoll 缓存仍按 FD 所有权规则失效。
- worker 拥有的等待不再迫使协调器扫描所有 timer FD 或提前暂停全部 CPU。旧 pselect、
  临时掩码、特殊资源等待、POSIX signal timer 和宿主控制周期保留原边界。
- 新增 relaxed deadline 和可执行槽位提示；只在到期或同槽 ready 任务竞争时进行局部
  任务交接。公平性交接要求当前任务已前进且分配运行满 250 µs，避免在未执行时反复
  重新排队。实际任务由原队列锁发布，无新增 guest 字节屏障、轮询总线或服务线程。
  无 deadline 和同槽 ready 竞争时，不新增宿主时钟查询。
- worker_timer_syscalls、worker_poll_syscalls 使用任务私有计数，汇总时合并。
  worker_task_yields 同时包含 guest sched_yield 与局部计时/公平性交接。

## 验证

Release 严格 Clippy（VFS/Linux/Core/CLI/FFI，含 tests）、CLI/FFI Release 构建、格式及
空白检查通过。队列的 ready/park/service 发布和预算路径受到改动，因此运行完整 Core
模块检查；其余只检查受影响模块。没有重跑已通过且实现未变的完整 CPU、Linux、VFS、
GPU、网络、CoreMark、Docker 或 Firefox 验收。

| 范围 | 通过数 | 内容 |
| --- | ---: | --- |
| Core | 127，另 2 ignored | 原预算、stop、服务、futex、任务交接；新增单宿主槽 read/epoll/ppoll 到期与 rearm、worker deadline 不结束无关 CPU |
| Linux direct | 14 | 共享 timer ABI 预检、相对/绝对时间、短读、rearm/disarm、ppoll 混合资源及 typed fallback；原文件/Unix/pipe/eventfd |
| VFS timer | 2 | 周期相位、overrun 累积/饱和、owner/worker 同状态、ET/ONESHOT、跨 fork last-close |
| VFS events | 3 | 原 timerfd 和 epoll 语义 |
| VFS direct | 7 | 6 项原检查，加修正测试预期后单独补跑的 1 项 |

共 **153 项通过**。最后一处 VFS 修改仅将旧测试里“timer 不支持”的假设改为验证 timer
支持，并改用 signalfd 测试特殊资源回退；生产代码没有变化。按工程验证规则，复用该
修改前已完成的生产 CLI/FFI 构建、48 组新对照及 12 组 Unix 回归，不为测试预期调整
重复构建生产产物或重跑已过项目。最终 Clippy 和 VFS 测试构建覆盖这处测试修改。

开发过程中两类检查暴露了实际调度问题：只缓存 deadline 时单槽 native peer 仍可能
占住执行槽；只处理 deadline 后，同槽 ready 任务仍会延迟到原宿主控制边界。加入局部
deadline 和 ready affinity 提示后解决，完整 Core 与最终集成均通过。测试指令预算从
1 million 调至 1 billion，以防预算耗尽伪装成预期的计时恢复；检查仍有 5 秒 host 上界。
另有 1 项旧 VFS 测试因新增 timer 准入而失败，按上述方式修正并补跑。原失败日志保留，
不作为最终通过结果。

## 固定工作量结构对照

新 fixture 使用静态 ARM64 原版 musl；主线程分别做 timerfd read、timerfd + eventfd
epoll、含负 FD 的混合 ppoll、空数组 ppoll 超时。每种做 32 轮，定时器/超时为 10 µs。
另一个线程执行固定 **512 次 1 MiB 原生填充（512 MiB）**，每次验证抽样字节，末尾
校验 checksum 及到期次数。工作量不随 elapsed time 改变。

四模式 × pinned/spread × 1/2/4 CPU，候选 **24 组**与上轮最终 Release **24 组**全部
通过。相同 ELF、输入和本机 C 断言/输出 oracle；native fault/reject/fallback 全为 0，
函数及写入字节计数完全一致。单 CPU 沿用 joined 路径。pinned 只允许 guest CPU 0，
多 CPU 配置检查 peak_parallel_cpus 为 1，不能把本地交接误认为两任务同时运行。

配置四 CPU 的一次结构计数如下；epoch 指全体返回的调度边界，不是调用者局部让出：

| 模式 | affinity | epoch 前 → 后 | 新本地 timer 调用 | 新本地 ppoll |
| --- | --- | ---: | ---: | ---: |
| timer read | pinned | 11 → 9 | 68 | 0 |
| timer read | spread | 104 → 9 | 96 | 0 |
| epoll | pinned | 11 → 9 | 68 | 0 |
| epoll | spread | 137 → 9 | 96 | 0 |
| ppoll | pinned | 11 → 9 | 68 | 22 |
| ppoll | spread | 137 → 8 | 96 | 32 |
| 纯 timeout | pinned | 9 → 9 | 0 | 23 |
| 纯 timeout | spread | 23 → 9 | 0 | 32 |

上述候选多 CPU 配置的 service_requests 和 serviced_syscalls 均为 0。timer 调用包括
read/get/set；主线程在计算 peer 退出后的尾部可能走 joined 路径，所以不是所有调用
都计入 worker。pinned 旧版本来就较少 epoch，不能从 spread 的变化推断普遍加速比。
250 µs 只在已有 CPU chunk/native 返回边界检查，不抢占原生函数内部。

额外运行 Unix sendmsg/epoll/WAITALL × pinned/spread × 2/4 CPU，共 **12 组回归通过**；
保持 3–4 个 epoch、零协调服务、准确字节/函数计数和相同输出。文件存储/OFD、Unix
channel、futex 等未变实现复用上轮结果，hash 核对见`worker-waits/results.json`（本地验证记录）。

这是结构与功能验证，没有墙钟统计或 iPad 应用加速结论；不是完整 guest CPU oracle。
未新增 QEMU、Apple ARM64/16 KiB 页或 iPad 真机验收，没有读取/归档私人 Firefox 诊断。

复现（构建需要 ARM64 musl 工具链；Unix 回归的 host oracle 需要本地 socket 权限）：

```sh
bash scripts/build-worker-wait-fixture.sh
cc -O2 -Wall -Wextra -Werror -pthread -DTRAMES_HOST_ORACLE=1 \
  tests/fixtures/worker-wait.c -o target/worker-wait-host
python3 tests/integration/worker-wait.py --runner target/release/trames-run \
  --oracle target/worker-wait-host --baseline /path/to/previous/trames-run \
  --output target/worker-wait-results
```

## 剩余集中边界

FD 创建/关闭/安装、SCM_RIGHTS、连接建立、host socket/PTY、pselect/临时掩码/未知资源、
POSIX 信号与 timer、映射和进程生命周期、未准入文件元数据/增长/持久化、stateful GPU
及 host relay pump 仍需继续拆。宿主控制周期仍可能结束 cohort。协调角色尚未移除；
这一轮移除了共享计时等待的逐次全体同步和不必要的 timer FD 扫描。
