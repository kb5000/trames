# 共享文件关闭通知与事件寿命

2026-10-23，第 27 批。拆出完整 FD/OFD 迁移的一项依赖：文件的关闭通知租约
`Arc<FileClose>` 不再包含 inode/path 的 `Rc/RefCell`，可以由工作线程最后释放。
生产 OFD 使用同一租约和事件实现；本批没有把实际 close/dup 系统调用迁到 worker。

## 实现

- inode 的监视目标是独立共享对象，持有弱事件队列引用。只有 inode 被监视、打开
  或用作目录链接的父对象时才初始化，未使用的 inode 保持无额外分配。
- parent/name/linked 构成一条链接的对象级事务。名称用共享不可变字节，关闭时不
  复制名称缓冲；最后关闭读取当前链接，保留 rename 和 IN_EXCL_UNLINK 语义。
- 打开后才添加的 watch 也在实时目标集合中。弱队列和普通文件/字节/poll 能力不
  延长关闭租约或 inotify 实例寿命。
- 释放 Node 元数据发布终态删除位；若真实关闭租约仍在，先发送 IN_CLOSE，再在
  最后事件对象释放时发送 DELETE_SELF/IGNORED。目录显式删除仍立即撤销 watch。
- 发送事件前释放注册集合和链接对象锁，队列亦在解锁后通知。没有全局 VFS 锁、
  服务线程、轮询总线或普通 guest 访存屏障。文件字节 worker 热路径仍使用已有
  的弱队列能力；新共享状态主要用于生命周期与快照准备。

这只是关闭通知组件的共享所有权，不等于完整 OFD、动态 FD 表、inode/namespace
及所有端点已共享。调用者只有在真实接受操作时才应取得 `close_notification()`
租约，不能给所有缓存能力增加强引用而推迟最后关闭。

## 验证

**29 个不同 VFS 定向测试通过**，覆盖通知队列、路径/rename/unlink、文件能力、
共享 FD 表及 flock 最后关闭。新增测试验证：

- worker 最后释放；打开后新增 watch；rename 到另一目录后使用新名称。
- 关闭之前不发送事件，IN_CLOSE → DELETE_SELF → IGNORED 顺序；目录删除例外。
- unlink/EXCL_UNLINK；已关闭实例不会被旧能力重新激活。
- 通知回调可重新访问注册集合和链接状态，证明未持这些对象锁通知。
- 32 轮并发 rename/关闭只允许原父目录＋原名称或新父目录＋新名称，不混拼。

复用已核对源码/产物哈希的原生及静态 ARM64 `inotify`、`worker-inotify` 程序。
原生 Linux 通过；两套程序分别在 guest 内存/磁盘 × 1/2/4 CPU 共 12 个配置通过。
worker 测试保持文件通知、事件等待与撤销的直接路径，service_requests 为 0。
未改动的通过项目复用；新增并发测试后只运行该新增测试，没有重新跑已通过的整组。

Release CLI/FFI、五 crate/all-targets 严格 Clippy、格式与差异检查通过。
没有重跑完整 CPU/GPU/Firefox/Docker，也没有给这一生命周期改造推导整体加速比。
未构建 IPA、提交或推送。

- 源码/产物/日志哈希：[`shared-close-notifications-summary.json`](shared-close-notifications-summary.json)
- 本地原始记录：`target/shared-close-notifications-checks/`
- 总进度：[`coordinator-removal.md`](../architecture/coordinator-removal.md)
