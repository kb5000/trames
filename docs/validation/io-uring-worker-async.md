# io_uring 提供缓冲、重复完成、计时/取消与 CQ 等待

2026-10-23，Linux 云环境，Release / cached interpreter / 无 JIT。
[机器记录](io-uring-worker-async-summary.json)保存源文件、产物、逐项证据及复用范围。
本轮是协调器拆分第十七批：worker 扩至 17 个 opcode，兼容实现仍探测 30 个。

## 实现

PROVIDE/REMOVE_BUFFERS、POLL_REMOVE/UPDATE、TIMEOUT/REMOVE/UPDATE、LINK_TIMEOUT、
ASYNC_CANCEL，以及 RECV/POLL_ADD/TIMEOUT multishot 接入原 worker ring。
经典/注册提供缓冲复用原组/槽算法和 SharedPage，组仍绑定原 VM；不复制整个 VM，
数据区也不被隐式 pin。同组在途 lease 防止重复分配，数据/文件/socket 操作在 ring
锁外。NOP、计时/取消和池元数据在同一次对象锁内处理，避免额外领取/归还往返。

MORE 保留原链接依赖，终结后才释放；取消的 CQE 不携带旧 MORE/buffer ID。序号
索引覆盖其他 worker 的在途请求；取消不可取消的在途操作返回 EALREADY，不假造
ECANCELED，不重放字节效果。多次请求在 issue 前预留额外 CQ 空间，使用 64 次
推进预算及游标轮转；背压、未来依赖及占用的组不订阅立即就绪的宿主资源。

相对/计数超时在实际 issue 时开始，EXT_ARG 的 timespec 只导入一次，随后携带绝对
deadline 跨 cohort 交接，不重读、不重置。解析和提供缓冲算法由 owner/worker 共用。
REGISTER_PROBE、UNREGISTER_BUFFERS、UNREGISTER_EVENTFD、PBUF_STATUS 和
UNREGISTER_PBUF_RING 五类无需安装 FD/固定新页的控制也可留在 worker。

CQ 的 ppoll/epoll 复用原 Poll/WatchState，订阅 CQ 与请求的原资源/宿主事件，再用
当前任务的 View 推进请求并检查事件。其他任务已有保留的 enter actor 时可观察其
完成；无执行上下文的外来后台请求仍需要生命周期检查点，不借观察者 VA 执行。
没有新增后台执行器、服务线程、轮询总线、全局数据锁或普通访存屏障。

## 精确计数找到的回退

旧筛选只允许 Unix stream，已连接 datagram 会退回 owner。现在 ring 使用独立的
消息能力，保留原队列/权限/rights 规则、空消息及 MSG_TRUNC 的复制/报告长度区别；
普通标量 syscall 准入保持原样。另补齐空资源与坏 selected VA 的顺序：先保持
EAGAIN 和原池，资源就绪后再以 EFAULT 完成，不能提前消费槽或源字节。

guest sleep 唤醒时，旧 drive 循环会先跑一个 owner 串行 quantum，导致压力测试少
两次 worker enter。现在仅在带 ring 的任务刚恢复时重查并行准入；默认无 ring 的
计算不增加逐指令检查。真实 ring host event/deadline 可以继续在全体 CPU 睡眠时
接管，单活跃 CPU 与无关 idle waiter 的原 MMU 准入保护仍保留。

## 验证

50 项不同 Rust 检查通过：35 项 worker ring、3 项原注册边界、8 项 VFS 并发字节/
epoll、4 项 core 准入/截止时间。结合下述 guest 检查，覆盖 VM 隔离、空资源与坏 VA、原 pin/描述符身份、
解除注册后保留已接受 pin、EAGAIN/EOF、MSG_TRUNC、普通/硬链接、计数/相对计时、
更新/取消、已领取 I/O 的 EALREADY、同组并发 lease、在途 DRAIN、满 CQ、预算轮转、
CQ ppoll/epoll、不同任务的内存边界及一次导入的 EXT_ARG。

最后仅调整 selected-buffer 的冷故障分支，新增回归在最终源码上通过；49 项不受
影响的本轮已通过结果直接复用。测试名称去重，0-test 调用和重复执行不计入数量。

24 组 ARM64 guest 用例通过：新 fixture 的 1/2/4 CPU ABI、IPv4/IPv6 CQ poll 与
满 CQ native TCP 共 15 组；上一批固定文件/缓冲 fixture 共 9 组。最终冷故障调整后
只重跑受影响 ABI 的 3 组，其余 21 组保留本轮 runner/fixture 哈希并复用。

新 fixture 每个 ABI 有 699 次 enter 和 8 次适用的 register 控制，包含 128 批
multishot/link/cancel/池补充/移除压力。guest 自己计数，宿主断言每次调用都留在
worker。stdout 的 libc 元数据初始化在 worker 阶段前完成，FD 创建/注册新 pin 等
生命周期边界仍执行。TCP 服务端分隔 30/20 ms 返回 `p`/`ong` 后半关闭；guest 使用
CQ ppoll 再 epoll 读取部分结果和 EOF。满 CQ 分支保持 unread CQ 并 sleep 150 ms，
检查不忙等，醒来后继续回收原缓冲，验证前述调度修复。

| 新 fixture | guest 调用计数 | 实际 worker io_uring | worker ppoll/epoll | 协调服务请求 |
| --- | ---: | ---: | ---: | ---: |
| abi-1 | 707 | 0 | 0 | 0 |
| abi-2 | 707 | 707 | 0 | 0 |
| abi-4 | 707 | 707 | 0 | 0 |
| tcp4-1 | 3 | 0 | 0 | 0 |
| tcp4-pressure-1 | 11 | 0 | 0 | 0 |
| tcp6-1 | 3 | 0 | 0 | 0 |
| tcp6-pressure-1 | 11 | 0 | 0 | 0 |
| tcp4-2 | 3 | 3 | 2 | 0 |
| tcp4-pressure-2 | 11 | 11 | 0 | 0 |
| tcp6-2 | 3 | 3 | 2 | 0 |
| tcp6-pressure-2 | 11 | 11 | 0 | 0 |
| tcp4-4 | 3 | 3 | 2 | 0 |
| tcp4-pressure-4 | 11 | 11 | 0 | 0 |
| tcp6-4 | 3 | 3 | 2 | 0 |
| tcp6-pressure-4 | 11 | 11 | 0 | 0 |

单 CPU 按原串行入口执行，worker 计数为 0。2/4 CPU 全部计数精确匹配，测试中的
协调服务请求均为 0；这不包含 FD/映射生命周期检查点和正常 cohort 准备/回收。
`worker_uring_syscalls` 现在含上述 register 控制，`worker_poll_syscalls` 也含
epoll_pwait/pwait2，历史报告仍使用其原口径。park/调度次数随运行变化，不作吞吐或
CoreMark 提速声明；本轮未重跑完整 CPU/GPU/Firefox/Docker/CoreMark 或全 syscall probe。

Release CLI/FFI、六 crate all-target 严格 Clippy、格式、C 警告、Python/shell 语法及
diff 检查通过。原始证据留在 `target/io-uring-async-checks/`，没有构建 IPA 或作 iPad 验收。

## 剩余同步边界

setup、新 pin、固定文件表/新 FD 安装、路径/连接控制、ACCEPT multishot、原生 TCP/
普通文件 POLL_ADD、受限/增长/同步/持久化文件服务，以及未保留执行上下文的跨任务
后台请求仍沿原入口。它们需要进一步共享 FD/OFD、文件和任务生命周期，不能通过
把 Rc/RefCell 标成 Send 或借当前 View 来跳过。

本轮拆掉的是上述长期异步队列和等待的常用协调路径，尚不能称整个 io_uring 或
整个协调器已完全撤销。总体边界见[io_uring 设计](../architecture/io-uring.md)和
[协调器拆分进度](../architecture/coordinator-removal.md)。
