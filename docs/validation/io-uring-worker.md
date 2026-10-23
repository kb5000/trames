# io_uring worker 本地执行验证

2026-10-23，Linux 云环境 Release / cached interpreter / 无 JIT。
[机器记录](io-uring-worker-summary.json)保存源文件、产物哈希和逐项结果。
这是首批协调路径迁移的历史记录，兼容实现仍探测 30 个 opcode；Apple/iPad 验证尚未进行。
后续固定资源、链接/排空的迁移见[本轮记录](io-uring-worker-registered.md)。

## 实现边界

worker 可消费无链接 NOP、READ/WRITE、READV/WRITEV、SEND/RECV 和单次 POLL_ADD，
资源来自原 pipe/eventfd/timerfd、已连接 Unix、原生 TCP 与 FileEpoch 普通文件能力。
队列、CQ 发布和资源订阅使用原 SharedPage/Readiness/HostEvents，不传递 Rc/RefCell，
不新增服务线程、后台 ring 执行器或每请求 RPC。

同 ring 多消费者在 kernel 游标处互斥，数据/文件/socket 操作在锁外执行；已经领取的
请求仍计入接受/元数据额度。GETEVENTS 先订阅再重试，然后让出执行槽；CQ-only
waiter 也能被另一提交者的完成唤醒。timerfd 的 deadline/重设走原运行队列。
全部 guest 线程已睡眠时，真实原生 ring 等待及相关 peer 仍可采用事件驱动 cohort。
单活跃计算和无关 idle waiter 保留原 Memory/MMU 准入条件。

混合模式在消费 SQ 前回到原入口；已接受的受限文件/消息请求保留原 OFD、导入的
向量及提交计数，在 cohort 回收后交回，不重放。下一 cohort 可接管适用的原生等待，
原预检查不先做一次相同的字节操作。关闭/复用数字 FD 不改变已接受请求的对象。

## 定向测试

16 项不同 Rust 检查通过：13 项 Linux worker ring 检查、1 项新 core 准入检查，
以及 2 项现有 GETEVENTS 原 ring/EXT_ARG 截止时间检查。覆盖 CQ 溢出/u32 回绕/
坏 head、整批无效果回退、并发消费者、源缓冲预检、FD 复用、VM 隔离、epoch 析构
恢复、等待不重读 SQE、跨提交者 CQ 唤醒、下一 cohort 接管、普通文件位置/增长回退、
eventfd 计数、poll flags、timerfd 截止时间/重设，以及 TCP 扩展 flags 的执行前回退。

已通过且代码路径未变的 10 项 Linux 与 3 项 core 结果在最后的局部调整后直接复用；
额外 3 项边界检查、受影响 cursor 检查与最终 guest 等待检查覆盖后续变化。具体用例
和复用范围见机器记录，没有将重复执行计入不同测试数量。

18 组不同 ARM64 guest 检查通过：worker fixture 的 1/2/4 CPU ABI + IPv4/IPv6 TCP
共 9 组，以及原始/扩展/multishot fixture 的 1/2/4 CPU ABI 共 9 组。最终 Release
runner 跑了其中 13 组；5 组未改动的单 CPU owner 路径复用本轮前面的结果，各自保留
原 runner/fixture 哈希。新增文件测试最初缺少最小 rootfs 的 `/tmp` 初始化，修正 fixture
后只重跑受影响的 ABI 分支；不是产品文件创建回退。

worker fixture 每轮一读一写，共 128 轮，另有 8 次单次 poll、8 次普通文件覆盖写、
8 次按位置读取，共 280 次 enter。CPU 2/4 全部留在 worker，文件位置保持初始 64。
TCP 服务端延迟 30 ms 返回 `p`，再延迟 20 ms 返回 `ong` 并半关闭，用来覆盖跨
cohort 订阅恢复、分段读取和 EOF，而不只是检查一次立即完成的 SEND。

| 用例 | worker io_uring enter | worker I/O park | 协调服务请求 |
| --- | ---: | ---: | ---: |
| abi-1 | 0 | 0 | 0 |
| abi-2 | 280 | 249 | 0 |
| abi-4 | 280 | 245 | 0 |
| tcp4-2 | 4 | 11 | 0 |
| tcp6-2 | 4 | 11 | 0 |
| tcp4-4 | 4 | 11 | 0 |
| tcp6-4 | 4 | 11 | 0 |

上述计数证明测试中短调用/等待经过 worker 路径，不表示总体性能、IPC 或 CoreMark
提升。正常 cohort 控制周期及映射/进程边界仍存在。首次接入时 TCP 只有 SEND 留在
worker，后续延迟 RECV 回到 owner；修复无 ready CPU 的准入及预检查后，四组 2/4 CPU
TCP 的 SEND、分段 RECV 和 EOF 共 4 次 enter 全部留在 worker。

## 构建、复用与剩余范围

Release CLI/FFI、Linux/VFS/core/CLI/FFI all-target 严格 Clippy、格式、C 警告检查、
Python/shell 语法与 diff 检查通过。原始日志留在 `target/io-uring-worker-checks/`。
先前完整 CPU、GPU、Firefox、Docker、CoreMark 与全 syscall probe 结果未重复运行。

setup/register、固定/提供缓冲、链接/排空、multishot、io_uring timer/取消、EXT_ARG、
FD/VM 生命周期、原文件增长/持久化/大块磁盘服务仍使用已有路径。单任务串行入口也
保留；本轮没有构建 IPA、没有宣称完整 io_uring 或完全撤销协调器。下一批优先迁移
固定缓冲、链接与 multishot，并让其共享当前 worker 的完成/等待基础。
