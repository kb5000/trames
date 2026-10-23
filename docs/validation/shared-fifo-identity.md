# 共享 FIFO 身份与元数据分离

2026-10-23，第 33 批协调器拆分。删除 `Rc<RefCell<Pipe>>` 包装层，Pipe 成为
包含稳定 inode 编号与 Arc<PipeResource> 的可共享值；Node::Content::Fifo 不再
引用原所有者控制图。匿名 pipe inode 元数据移到打开端点的 owner 投影，原生
剪贴板 relay 显式保留相同元数据。机器记录见
[shared-fifo-identity-summary.json](shared-fifo-identity-summary.json)。

## 行为与边界

每条管道少分配一个 Rc/RefCell 包装对象；元数据访问不再借用该包装，splice
使用实际共享字节资源的 Arc 身份识别同管道，保留原来的 self-splice EINVAL。
普通字节 I/O、PIPE_BUF、backpressure、FIFO 打开握手、EOF/HUP/EPIPE 都复用原
资源和端点实现，没有新增页/全局锁、普通访存屏障、服务线程或忙等。

Pipe 身份和 O_PATH/目录节点不算打开端点，不延长读者/写者计数；共享资源引用
也不保活已 unlink 的 Node。匿名 pipe 两端和 fork/dup 仍访问相同元数据，relay
保留元数据但不额外保留已关闭的 guest 端点。命名 FIFO 的 inode 已由 OFD 持有，
不在共享资源中反向引用 Node，避免环和延迟删除。

新增检查发现普通 FIFO 的 link 曾被内容白名单错误拒绝为 EPERM，本批允许持久
FIFO 使用既有硬链接事务。硬链接共享同一资源，路径替换产生独立资源；unlink 后
旧 FD 继续可读写，最后关闭正常 EOF。磁盘恢复保留硬链接 inode 身份，但不恢复
旧管道缓冲/打开计数。仍保留原来的目录及 synthesized/volatile 节点限制。

完整 PipeEnd/OFD 元数据、路径、安装/关闭、阻塞 open 和 splice 的控制入口仍在
owner。此批使资源身份可共享并减少包装成本，没有因此宣布这些 syscall 或
io_uring 控制操作已经迁出，也没有整体性能提升的量化结论。

## 验证

19 项不同的 release Rust 检查通过：VFS 16、Linux 3。包括共享身份的跨线程
打开/读写/关闭，硬链接/路径替换/删除 inode 寿命，overlay 别名与 self-splice，
原生剪贴板 relay、元数据/proc 身份、磁盘恢复、并发关闭与打开握手、PIPE_BUF、
背压、SIGPIPE 和阻塞恢复。首轮硬链接检查暴露已有 EPERM；修复后只重跑该项与
扩展了硬链接恢复的持久化项，其余通过结果复用。

两个静态 ARM64 fixture 分别检查 pipe/FIFO ABI（含新硬链接用例），以及 M6 的
`fifos` 分支（fork 打开握手、信号打断、dup/unlink/EOF）。各自在内存/磁盘 ×
1/2/4 CPU 完成 6 组，共 12 组 guest 验证；输出与两个原生 Linux C 检查一致。
修改前 runner 通过旧 ABI 子集，并独立重现新硬链接用例的 EPERM，保留原始日志。
未运行原 fixture 的不相关网络、锁、父进程信号或 OOM 分支。

Release CLI/FFI、三个相关 crate 的严格 release Clippy（含测试）、fmt/diff
通过。原始材料位于 `target/shared-fifo-identity-checks/`。未重跑上批不受影响的
Unix pathname/网络 namespace、完整 CPU/GPU/Firefox/Docker/CoreMark；没有
制作 IPA、提交或发布。Linux x86_64 结果不代替 Apple ARM64/iPad 验收。
