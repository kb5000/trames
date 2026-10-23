# io_uring 等待订阅复用与协调依赖审计

日期：2026-10-23。本次不增加 opcode/注册能力，不另计迁移批次；当前开发代码仍为
17 批迁移，尚未进入新 IPA。机器结果见 [JSON](io-uring-wait-reuse-summary.json)。

## 改动与边界

enter 和 CQ ppoll/epoll 共用 `direct/uring/wait.rs`。原实现每次恢复都 clear/drop
订阅，再按请求 dup 宿主 FD、注册 epoll/kqueue；同一等待中的多个请求也可重复注册。
现在 actor 保留订阅，按原 OFD 与读写方向去重，仅对已触发的一次性订阅 rearm；
不同 actor 保持独立的 signal/one-shot。ring 与字节通知也复用，并在下一次等待资格
计算时删除已取消、完成、被依赖/缓冲组/CQ 背压阻塞的资源。

先订阅/rearm 后重试原资源；新代码不创建服务线程，不改变 guest 字节的同步规则，
不把不同 actor 合并到同一个 one-shot。OFD 身份由 cohort 原所有者保活，使用 FD 数字
作为缓存键会误绑复用后的描述符，故未采用。读写方向不同也不共享一次性注册。
PendingIo 释放时撤销所有等待 lease，不能把宿主 socket 保活到 guest 最后关闭之后。

CQ poll 初次推进与后续 PollWait 现在都按 ring 身份去重，同一 ring 的多个 FD 别名
不能重复使用 64 次推进预算。常见单 ring 入口直接执行，不为去重额外分配集合。
owner/worker 的资源引擎尚未合并；这里只统一等待管理，既有计时/缓冲/probe 继续共享。

## 定向正确性检查

- Release worker io_uring：40 项通过；其中 5 项是本轮新增的等待/别名回归。
- 新检查覆盖：重复恢复不重新获取 FD、one-shot 触发后 rearm、不同 actor 独立取消、
  EOF/最后关闭、读写方向和替换 OFD、同 OFD 的多个 TCP 请求、部分读取后继续等待、
  本地订阅去重/完成后取消监听，以及 CQ 别名不放大推进预算。
- 沿用并检查既有计时/链接/取消/提供缓冲/注册 pin/并行租约/背压等 worker 检查；
  1 项隔离微基准在默认测试中 ignored，本轮单独运行。
- 最终 Release CLI/FFI 构建、`trames-linux --all-targets` 严格 Release Clippy 通过。
  fmt 和差异空白检查也通过。

真实 ARM64 guest 在双核、四核上执行 10 个受影响用例：

| 用例 | worker io_uring 调用 | worker poll 调用 | service_requests | 宿主 CPU 时间 / 墙钟 |
| --- | ---: | ---: | ---: | ---: |
| abi-2 | 707 | 0 | 0 | 0.309 |
| tcp4-2 | 3 | 2 | 0 | 0.105 |
| tcp4-pressure-2 | 11 | 0 | 0 | 0.043 |
| tcp6-2 | 3 | 3 | 0 | 0.100 |
| tcp6-pressure-2 | 11 | 0 | 0 | 0.044 |
| abi-4 | 707 | 0 | 0 | 0.436 |
| tcp4-4 | 3 | 3 | 0 | 0.098 |
| tcp4-pressure-4 | 11 | 0 | 0 | 0.049 |
| tcp6-4 | 3 | 3 | 0 | 0.105 |
| tcp6-pressure-4 | 11 | 0 | 0 | 0.043 |

两个 ABI 用例分别精确完成 707 次 worker 调用；TCP 的 ppoll/epoll 在 worker 完成。
全部 service_requests 为 0。满 CQ 用例包含约 150ms 等待，CPU/墙钟比例显著低于
检查阈值 0.75；不通过忙转保持 cohort。计数口径沿用前轮，不把 SQE 数当 syscall 数。

普通单 CPU owner 路径、固定资源算法及 CPU/GPU/Firefox/Docker 全量验收未重跑；
未修改范围复用 [前轮记录](io-uring-worker-async.md)，不将那些旧结果算进这 10 项。

## 隔离成本比较

宿主：Linux x86_64，AMD EPYC 9V45 96-Core Processor，Release。
使用同一测试进程中的原 `dup + subscribe + drop` 与生产 `WaitSources::host` 保留路径
对比，每样本 100000 次、7 个样本，交替先后顺序；记录轮次无并发构建。

| 指标 | 每次重建 | 保留原订阅 |
| --- | ---: | ---: |
| 样本中位数，ns/次（整数显示） | 531 | 13 |

每样本比值中位数为 39.33 倍。这里只测空闲订阅的管理，
没有真实 I/O、内核就绪事件、触发后的 rearm syscall、完整 refresh 的请求扫描或
执行器调度；不能推导整体 enter/CoreMark/Docker 吞吐提升，也不能替代 iPad kqueue
验收。收益来自去掉重复的 dup、ADD/DELETE、close 与本地通知注册，而非减少必要的
字节/原子内存语义。

## 是否已经完全撤掉协调

没有。生产代码仍有 `workers::Event::Service`/`Request::for_epoch`，受限调用仍可交给
原所有者，FD/映射/进程生命周期也可让 cohort 整组返回。这些用例的 service_requests=0
只证明对应路径，不代表整个系统没有协调依赖。

剩余所有权边界和优先顺序见 [协调器拆分进度](../architecture/coordinator-removal.md)。
先共享 FD/OFD、文件及提交 VM 的可调度执行上下文，再缩小映射/生命周期变更的暂停
范围；继续复用资源算法，不通过全局 VFS 锁或重复维护两套 opcode 来追求表面覆盖。

原始产物在 `target/io-uring-wait-reuse-checks/`，runner、测试二进制与相关源码 SHA256
在 JSON 中。测试仅在本地执行，本轮没有提交、推送、打 IPA 或更新 GitHub Issue。
