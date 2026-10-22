# 阻塞 I/O 直接交还执行槽

> 本文为公开的验证汇总。提及的原始日志、二进制和逐项 JSON 记录保留在开发环境，未随本次源码提交公开。

2026-10-22，接续[共享运行队列](task-handoff.md)，本轮迁移 pipe/eventfd 的阻塞
read/write/readv/writev，以及只监听这些资源的 epoll_pwait/epoll_pwait2（signal mask
为空）。协调角色仍存在；源码是本地未提交工作树，没有构建 IPA 或发布 release。

## 已实现

- 阻塞调用保留自己的 CPU、执行 View、参数和 deadline，交还宿主执行槽。资源变化
  直接重新入队，当前或其他允许的 worker 完成原调用，不再经过协调 RPC/全体暂停。
  SVC 只退休一次；恢复时直接完成 I/O，不重新执行 SVC，也不重复消耗指令预算。
- 就绪和非阻塞字节 I/O、已就绪/零超时 epoll 直接完成，不分配订阅。仅在确需等待时
  创建订阅，先订阅再重试再 park。线程间通知不搬 View、不发消息、不创建服务线程。
  任务已运行时通知可以合并，竞争失败者继续等待，不返回虚构的 guest 竞态错误。
- 管道沿用原队列、PIPE_BUF、EOF/EPIPE 算法，统一 guard 在实际世代变化后释放资源锁
  再通知，splice/VFS/native pipe relay 同样生效。订阅锁也在进入运行队列前释放。
  无订阅的管道变化只有原子标志检查；等待不占着资源锁或宿主 worker。
- eventfd 计数保留原 acquire/release RMW；订阅标志 RMW 握手覆盖 ARM 的先订阅/后检查
  竞争。通知位 release RMW 发布、重试前 acquire RMW 清除，覆盖 running 任务合并通知
  再次等待的窗口。普通 guest 字节、原生函数同步模型和 CPU/MMU 未改变。
- epoll 的 ET 世代、ONESHOT 和轮转游标由 joined/worker 路径共享，多个 waiter 只有
  一份 delivery 状态；ctl 修改后重新生成资源快照。fork 后另一张 FD 表的最后关闭
  也失效相关快照，资源 Arc 不被误认为仍打开的 OFD。订阅不增加管道读写端点数。
- epoll timeout/buffer/16 字节 ARM64 event scatter 共用 Linux helper，保留 canonical
  guest 地址校验。超时唤醒 pending 调用做最终资源检查。预检失败不消耗 ONESHOT。
- cohort 启动时可直接接管已经阻塞的 unfiltered/untraced 共享资源调用，不先在协调器
  重试字节 I/O。跨 VM 的管道也可接手；View 仍全部在运行前准备，运行中不克隆内存。
  新调用沿用 immutable seccomp ALLOW 准入；已有过滤器/trace 的旧等待保留 joined 路径。
- 完成/取消/cohort 结束移除订阅，等待中资源不额外保留 guest OFD。全部 View 释放后
  才提交内存/修改映射；SIGPIPE 保留原 joined 信号处理，已完成或部分完成 I/O 不重放。

## 固定工作量对照

Linux x86_64 Release，同一个静态 ARM64 musl ELF，两个 guest 线程做 1024 轮交替
接力；每种模式共 2048 次原生 4 KiB 填充（8 MiB），校验发布数据与固定 checksum
261120。pipe/eventfd 模式直接阻塞读写；epoll 模式用 ET 等待管道，再读取令牌。
独立 host C 程序提供断言及输出 oracle，没有执行新 QEMU CPU oracle。

改前二进制保留自本轮开始，包含上一批 futex/运行队列优化。每组只保留最终产物一次
独立采样，编译和功能测试结束后计时；早于最终关闭缓存修正的采样已弃用。

| 模式 | 配置 CPU | 改前 ms | 改后 ms | 改前全局 epoch | 改后全局 epoch |
| --- | ---: | ---: | ---: | ---: | ---: |
| pipe | 2 | 24.994 | 13.190 | 1026 | 5 |
| pipe | 4 | 26.514 | 13.100 | 1027 | 5 |
| eventfd | 2 | 32.594 | 13.082 | 1026 | 5 |
| eventfd | 4 | 24.428 | 13.028 | 1026 | 5 |
| epoll | 2 | 33.966 | 15.216 | 1029 | 5 |
| epoll | 4 | 29.639 | 14.887 | 1046 | 5 |

本机这一个等待交接场景约 1.9～2.5 倍；不能外推为 Docker、CoreMark、Firefox 或 iPad
提速。两线程串行接力，不是四核持续计算扩展性测试。单 CPU 仍用原 joined 路径，三种
模式改前/后约 4.8～6.7 ms，没有足够样本判断小幅差异。多核每组约 2046 次 I/O park，
重试与唤醒不需要协调 RPC，RPC 均为 0；原生 2048 次填充、8 MiB、结果一致，fault/
reject/fallback 均为 0。指标和 SHA-256 见`io-handoff/comparison/results.json`（本地验证记录）。

## 验证与构建

- core 119 项通过、2 项既有手工项目忽略。确定性单宿主槽测试分别完成 pipe/eventfd/
  epoll 等待、写者运行、yield、原调用恢复；只收到最终 EpochStopped，没有 Service，
  恰好 7 条指令、剩余预算 93。epoll 超时案例恰好 3 条指令、预算 97。
- 运行队列覆盖 I/O wake-before-park、stop 保留调用参数/释放订阅；VFS 覆盖回调释放锁
  后重入、订阅取消、4096 轮原子订阅竞争、ET/LT/ONESHOT、公平游标、并发一次性消费
  及另一张 FD 表最后关闭的缓存失效。Linux 覆盖 eventfd 满计数写等待、pipe EOF、
  PIPE_BUF 背压重试、EPIPE 回到信号路径、epoll deadline/布局/错误预检。
- VFS 首轮全量 181 项通过、1 项忽略；之后通知顺序改动只复查 readiness 2 项，最后
  关闭缓存改动只复查 direct 7 项。Linux 首轮全量 34 项通过，最后只复查受影响 direct
  6 项（含新增背压用例），其余 29 项复用。没有因为格式/文档变化重跑已通过测试。
- 最终四核 SMP 集成通过，覆盖 affinity、原子/内存发布、futex、并发 fork COW、shared
  process atomics、pipe、exec/exit_group。CPU/输出 oracle 复用先前记录，未声称执行新
  QEMU；native 19 函数、其他 CPU/MMU/图形项目复用既有验收，没有重新跑全工作区。
- 五个包 vfs/linux/core/CLI/FFI 的 tests Release 严格 Clippy 通过，Release CLI/FFI
  构建、全工作区 fmt 检查及 diff 空白检查通过。最后只作 rustfmt 排版；编译时/当前
  源码分别记录 SHA-256，并验证排版前源码经 rustfmt 后与当前内容逐字节一致。

`io-handoff/results.json`（本地验证记录）区分各阶段复用范围、产物和源码散列。没有 Apple
ARM64、16 KiB 页、iPad 真机验收；没有发布、推送、IPA，也没有读取/复制私人 Firefox
诊断包。这里只针对本轮改动验证，未重新运行 CoreMark、Docker、Firefox。

## 后续边界

混合 socket/PTY/timer 等监听的 epoll、临时 signal mask、共享 key futex 仍需迁移。
文件请求仍会占用调用者宿主槽等回复，GPU/lifecycle/映射/权限/FD 表也仍有 joined 边界。
长期等待者的 View 仍预先准备，公平性/外部事件仍结束 cohort；后续继续共享对象状态，
将服务完成接入运行队列，再缩小每 VM 租约撤销范围，最后撤掉剩余专用协调角色。

复现（先保留改前二进制，输出目录不得存在）：

```sh
bash scripts/build-io-handoff-fixture.sh
cc -O2 -Wall -Wextra -Werror -pthread -DTRAMES_HOST_ORACLE=1 \
  tests/fixtures/io-handoff.c -o target/io-handoff-host
cargo build -p trames-cli -p trames-ffi --release
python3 tests/performance/io-handoff.py --baseline /path/to/trames-run-before \
  --runner target/release/trames-run --oracle target/io-handoff-host \
  --output target/io-handoff-comparison
```
