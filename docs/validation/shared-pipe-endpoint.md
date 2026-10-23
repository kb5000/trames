# 管道实际端点的共享所有权

2026-10-23，第 20 批之后的所有权改造，不另计 syscall 迁移批次。
证据见 [JSON](shared-pipe-endpoint-summary.json)。**完整 FD 表、close/dup 和 io_uring
的 owner 保留引用尚未迁完，本轮不能据此宣称真实 guest close 已全部在 worker 执行。**

## 改动

pipe.rs 区分 inode/路径的 owner 投影 Pipe/PipeEnd 与真实 Arc<PipeEndpoint>。
读写端计数、打开世代、FIFO 握手和最后关闭属于后者；最后一个引用在哪个线程释放，
就在哪个线程更新端点计数、EOF/HUP/EPIPE 并通知现有资源等待者，不发送 owner 消息。
克隆 lease/dup 不增加读写端计数，独立 open 才增加；普通字节/poll/标志快照和订阅
不持有真实端点。数据缓冲仍使用原 PipeResource/PipeData 算法。

- 非阻塞 FIFO 的 ENXIO 检查和新端点计数在一次 pipe 事务内，不能被并发最后关闭插入。
- FIFO 初始等待世代不可变，握手完成仅清除一个 relaxed AtomicBool；不为它增加
  第二个资源锁或访存屏障。读写可见性和世代状态继续由原 pipe 锁负责。
- 最后关闭通知沿原 PipeGuard，在释放 pipe 锁后调用，允许回调重入相同管道。
- VFS/直接字节快照/epoll 世代直接访问共享 pipe data，去掉 I/O 中不必要的 owner
  Pipe RefCell 借用；inode 路径、权限、inotify、FD 表和完整 OFD 仍保留原所有权边界。
- 原生剪贴板管道转发使用真实 Arc 端点，单独保留元数据 anchor，保持原 inode/
  unlink 生命周期。转发 pump 本身仍在原路径，没有引入新线程或轮询总线。
- RetainedFd 可以显式导出真实 pipe endpoint lease。它是端点寿命引用，不等于完整
  OFD/FD 权限对象；现有 io_uring 的 owner.originals/descriptor 引用仍保留，未改成
  用一个过度保活的普通能力快照替代它们。

没有给 Rc/RefCell 图声明 Send，也没有在普通 CPU/MMU 访存添加操作。一个新 pipe end
目前同时分配 owner 投影与 Arc 端点；这不是零分配创建路径。本轮没有应用性能提升
数据，后续完整共享 OFD 才能去掉多余投影与保留引用。

## 受影响验证

111 项不同 Release Rust 检查通过：VFS 52、Linux 57、core 2，包含 6 项新增。
新增测试验证：

- owner 投影已消失、元数据与普通快照仍保留时，worker 最后释放 writer 立即唤醒
  reader 并产生 EOF/HUP；回调可以重入该 pipe，不依赖 owner 确认。
- worker 最后释放 reader 后产生 EPIPE，最后两端关闭清空缓冲。
- 多个 alias/lease 只计一个端点，8 个线程并发释放；独立 open 计两个端点。
- FIFO 对端在重试前已经 open/close，握手仍按原世代完成；非阻塞 open 与最后
  reader close 竞争 128 轮，结果为成功或 ENXIO，最终计数始终归零。
- 普通字节/poll/标志快照与订阅不会额外打开端点。

其余检查覆盖直接字节/pipe/事件等待、FD/clone、proc magic links、原生剪贴板转发，
以及 worker io_uring 固定文件、提供缓冲、取消/链接/超时和等待交接。两个宿主 socket
相关检查在默认受限沙箱中失败，同一二进制在启用本地网络能力后通过；未为此改源码。

真实 ARM64 guest 的 1/2/4 CPU 配置通过：

- 原 m6 fixture 仅选择 `fifos` 用例，与宿主原生输出相同：阻塞打开、信号中断、
  非阻塞 ENXIO、fork、EOF、poll HUP、dup、unlink、FIONBIO/CLOEXEC。
- 原 worker-fd-table 的控制与生命周期两模式，与原生一致，继续验证 clone 四组合、
  成功/失败 exec、UNSHARE 和 448 次多 CPU worker 标志控制。
- 原 io-uring-worker 的共享队列 ABI 用例，实际多 CPU worker enter 计数符合原断言。

Release CLI/FFI、五 crate 的 Release all-targets 严格 Clippy、fmt 和差异检查通过。
复用未改动 ELF；没有完整重跑 CPU、GPU、Docker、Firefox 或 CoreMark。环境仍为
Linux x86_64，不替代 Apple ARM64/iPad 的性能与真机验收。原始输出位于
`target/shared-pipe-endpoint-checks`。

## 下一步的实际边界

共享 FD 表必须拥有真实 OFD 引用；lookup 为正在调用的资源取得稳定引用，close 后
的新 lookup 不得借旧数组访问已复用的 FD。dup2/dup3、分配和 CLOEXEC 域也要使用同一
表对象。等待中的字节调用应持有原 OFD，重试不能重新按数字 FD 绑定到新对象。

最后关闭还要处理 epoll weak watch、inotify、进程锁和 socket/PTY 等端点；回调不能在
FD 表或资源锁内进入运行队列。不能只移除表项、把真正释放留给下一次全体 join。
剩余完成判据继续见 [共享内核状态](../architecture/shared-kernel-state.md)。
