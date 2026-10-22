# 共享对象、worker 间消息与协调器通信成本

> 本文为公开的验证汇总。提及的原始日志、二进制和逐项 JSON 记录保留在开发环境，未随本次源码提交公开。

2026-10-22，本轮只做手工实验及架构原则整理，没有迁移生产 syscall、修改调度器、
构建 IPA 或发布。原生函数应留在执行 guest 的同一宿主线程；通信仅用于需要协调的
状态与资源。探针不构成另一套运行时后端。

## 三组的准确含义

| 组 | 实现 | 处理位置 | 额外服务线程 |
| --- | --- | --- | ---: |
| 共享对象 | 对象级 Mutex；另测单字段 Relaxed atomic | 调用者 worker | 0 |
| worker 间消息 | 每 worker 的 mpsc 收件箱；另测原子 mailbox | 资源所属的现有 worker | 0 |
| 原版协调器传输 | 生产 Request / Reply / Event / Pool 与 View 移交 | 父线程充当协调器 | 1 个活动协调角色 |

消息组只有原有的 N 个 worker：独立资源由下一个 worker 持有，构成互相请求的环；
共享资源属于 worker 0。每个 worker 同时是调用者和收件人，等待回复时继续处理进入的
请求，自己的调用结束后仍服务其他 worker，直到所有请求已回复。没有专门的资源 actor
线程，也没有父线程代为派发。1-worker 的消息组是本地投递对照，不代表跨线程延迟。

mailbox 每调用者至多一个未完成请求，使用 sequence 和 acquire/release 发布参数及
结果；这些同步用于消息协议，不增加 guest 普通访存屏障。该组持续轮询，是低延迟
实验边界，不能直接作为生产空闲等待策略。生产设计仍需要在已有 worker 执行循环中
处理通知，并在没有工作时休眠；等待中的 guest 任务也应让出执行槽。

协调器组复用真实的通道、请求 Box、回复对象和 View，但只执行与其他组完全相同的
计数操作。它**没有执行 Linux/VFS helper、解释器，也没有 stop/join、内存提交及重新
派发**，因此是原版服务通信成本的下界，不是完整协调器或真实 syscall 的总成本。
实验中的私有操作号不是新增 guest syscall。当前宿主 sizeof(Request)=6496、
sizeof(Reply)=6448、sizeof(View)=6416 bytes；没有据结构大小推断精确 memcpy 次数。

## 固定工作与结果

Linux x86_64 云端宿主，Release（工作区 thin-LTO），可用 affinity 为 5 CPU，
cgroup quota 为 400000/100000，最多相当于 4 CPU 的执行额度。不是 Apple ARM64 或
iPad 测量。每 worker 固定 20000 次 u64 自增并取回结果，测试 1/2/4 worker、独立/共享
资源。每组合 5 次交错顺序，共 150 个样本；每路径预热 256 次。没有自适应迭代数，
计时不与编译或其他 guest 测试并行。

每个样本校验每个资源最终值，以及全部返回值总和，检查丢失、重复或错误回复。
原子组的整个状态就是单字段计数器，Relaxed 足以满足该实验；不能据此推广到多字段
不变量或 futex compare/enqueue/wake。

下表为中位数，单位 ns/op。墙钟是**总耗时÷总操作数，即聚合吞吐的倒数**，不是
单个请求的延迟或 p95。CPU 是父线程和每个 live worker 各自线程 CPU clock 的时间
之和除以操作数；持续轮询会在这项账户里体现额外消耗。

| 路径 | 4 worker 独立资源：墙钟 | 独立资源：CPU | 4 worker 共享资源：墙钟 | 共享资源：CPU |
| --- | ---: | ---: | ---: | ---: |
| 共享 Mutex | 6.28 | 10.51 | 38.92 | 135.59 |
| 共享 atomic | 3.06 | 5.14 | 9.35 | 22.40 |
| worker 间阻塞队列 | 6309.66 | 9610.44 | 3797.06 | 5017.33 |
| worker 间轮询 mailbox | 98.06 | 382.68 | 98.01 | 384.23 |
| 原版协调器通信下界 | 6143.82 | 9007.64 | 6120.58 | 8950.47 |

五次样本存在宿主调度波动，没有统计置信区间，不挑最小值。所有 1/2/4-worker
样本、范围、CPU 账户、源码和二进制哈希见 `coordination-cost/results.json`（本地验证记录），
原始 stdout 见 `coordination-cost/run.log`（本地验证记录）。没有将此前误用额外资源服务线程的
探测、process clock 对短生命周期线程记账不完整的探索组混入本表。

开始迁移生产资源后，精确测量时的 `coordination-cost/source/workers.rs`（本地验证记录） 与
`coordination-cost/source/coordination_cost.rs`（本地验证记录） 另存快照；其摘要与 results.json
匹配。这些是历史证据，不参与生产构建，未因后续运行时代码变动重跑这批测量。

## 对架构选择的影响

- 短操作直接共享对象，避开跨线程交接，仍是首选。Mutex 只覆盖真实资源不变量，
  atomic 只用于其确实能表达的状态，不给整个原生算法套全局锁。
- 使用现有 worker 的低延迟消息可以明显降低交接成本；普通阻塞队列仍受宿主唤醒/
  调度影响，不能仅凭“没有协调线程”就假定通信很便宜。
- mailbox 在这组短操作里消耗接近 4 个 CPU 的时间；其低延迟不免费。后续应区分
  唤醒/失效等异步通知和需要回复的调用，并考虑 worker 正在执行其他 guest 工作时的
  收件箱响应时间。不能为追求探针数字而把生产 worker 永久放在忙等循环中。
- 保留的冷 RPC 可另行减少大对象搬运，但不以此代替 worker 本地执行及共享状态拆分。
  全体暂停、backing 提交和任务重派发还需独立归因。本表不是 Docker、Firefox、
  CoreMark 或 iPad 的加速比，没有证明可从生产移除某一既有一致性边界。

## 复现与验证范围

先构建 Release core 单测二进制，再将 Cargo 输出的可执行文件路径传给 runner：

```sh
cargo test -p trames-core --release --lib --no-run
python3 tests/performance/coordination-cost.py \
  --binary /absolute/path/to/trames_core-test-binary \
  --output target/coordination-new --iterations 20000 --samples 5
```

输出目录必须不存在。探针标记为 ignored，不随普通测试自动跑分。
结果一致性用例及受影响 core tests 的严格 Clippy 通过，runner 的 Python 语法和结果
文件哈希已检查。复用源码未改的 CPU、MMU、Linux/VFS、GPU、UI 与生产调度器通过
记录，没有为本轮实验重跑这些套件。未进行 Apple 或真机验收。
