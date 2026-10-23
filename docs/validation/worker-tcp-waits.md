# TCP 宿主事件等待与 worker 恢复

2026-10-22，第十四批协调器拆分。合格 cohort 内已连接、无活动规则的原生 TCP
read/write/readv/writev，在 EAGAIN 时保留原任务/View，让出执行槽，并由宿主
epoll/kqueue 事件直接入队。已有空闲 worker 驱动等待，不新增服务线程。
[设计与边界](../architecture/epoch-services.md)。

## 验证范围

20 项不同 release 单测通过：宿主事件 4、运行队列 9、TCP owner deadline 1、
Linux 原字节 ABI/无事件源 checkpoint 1、单活跃 CPU 准入 3、原 poll/socket
deadline 边界各 1。测试包含并发 subscribe 与写入竞争 256 次、多个同连接 waiter、
一次性 rearm、写背压、半关闭 EOF、取消、deadline、数据先于 park、停止/本地 pipe
通知打断宿主等待、共享连接继续交接，以及无关等待者不触发 parallel View。

阻塞 guest 对照使用同一个静态 ARM64 musl ELF：128×1024 字节 write/writev，
另一个线程持续原生 memset；减小发送缓冲产生背压。独立宿主 loopback 服务先延迟
接收，核对全部字节和 FIN，再分两段延迟回传。guest 检查部分 read/readv、完整回复
和最终 EOF。覆盖 IPv4/IPv6、1/2/4 CPU，原生 C 编译产物提供参考输出。

首轮修改前后 24 次 guest 运行通过。随后补齐共享 socket 的单活跃任务准入，只重跑
受影响的 2/4 CPU 八项；复用未变 ELF 的 4 个原生参考、单 CPU 和旧版结果。最终
八项每次均完成 133 次本地 TCP 调用、15 次 park、5 次直接恢复，服务请求为 0。
完整统计见 [worker-tcp-waits-summary.json](worker-tcp-waits-summary.json)。单次调度计数
用于确认路径，不能换算成吞吐、CoreMark、Docker、Firefox 或 iPad 加速比。

整组返回次数为 15–16，旧版对应 6–8。这里旧版会把阻塞连接留给 owner，CPU peer
独自回到串行执行；新版保持共享 socket 的 cohort，外部延迟期间仍每 10 ms 执行控制
checkpoint。因此本样本的返回次数没有下降，也不是整体速度提升的证据；下一阶段
仍需缩小这些统一暂停的范围，不能以本地调用增多代替性能结论。

Release CLI/FFI 构建、7 个相关 crate 的严格 Clippy（包含测试）、格式、脚本语法
和差分检查通过。未重跑无关完整 CPU、图形、浏览器、Docker 或 CoreMark 验收。
Apple kqueue 分支已实现，但本轮环境只有 Linux；Apple ARM64/16 KiB 与 iPad 尚未验证。

## 调度与生命周期边界

宿主事件后端按 cohort 懒创建。空闲线程等待真实事件/deadline；有可运行任务时在一次
调度边界做一次非阻塞分派，随后执行任务，避免单槽/满负载时饿死网络等待者。注册使用
独立 duplicate FD 和不复用的 ID，通知后按需 rearm；回调在 registry 和队列锁之外。
取消先删除注册再关闭 FD，cohort 全部停止后才允许 guest FD/namespace 变更。
普通 capability 仍为 Weak，不因为缓存延迟最后一次 guest close。

socket timeout 保留绝对截止时间，包括跨 cohort 重试。已迁移等待不再触发旧 owner
每 1 ms 网络扫描或其 socket deadline 全组停止；正常 cohort 控制周期仍存在。
共享 socket 身份用于调度关系，不缓存权限/防火墙决定，不改变普通 guest 访存模型。

单任务串行入口、UDP、DNS、消息 ABI、连接/控制、混合网络 poll/epoll、规则/虚拟
网络以及信号路径仍有原所有者参与。本批没有完全撤掉协调器。

复现：`scripts/build-worker-tcp-fixture.sh`，然后执行 `tests/integration/worker-tcp.py`
的 `--blocking --runner … --baseline … --oracle … --output …`。跟进检查可用
`--cpus 2 4 --reuse-oracles …`，后者核对 fixture/oracle 哈希后复用参考输出。
原始日志保留于本地 `target/worker-tcp-waits-checks/`，仓库摘要不包含原始诊断包。
