# 共享 inotify 实例与注册索引

2026-10-23，第 28 批。实际 inotify 实例由 `Rc<RefCell<Instance>>` 改为
`Arc<InotifyInstance>`；索引只持有共享 inode 通知源的弱引用，不再引用 Node。
队列和实例寿命明确分离，为完整匿名 OFD/FD 表迁移移除一项 owner 依赖。

注册/分配 watch 编号在实例索引锁内完成。现有 watch 的存在检查和 mask 更新在同一
队列事务中执行；已被 worker 消费的一次性 watch 不会在发出 IGNORED 后被旧编号
重新激活。Queue 不在通知时持锁，实例撤销也不持索引锁通知，回调可注册替代 watch。

worker `inotify_rm_watch` 使用与 owner 相同的完整撤销实现，立即清掉注册索引和
inode 标记，不再只改队列、留下索引等 owner 清理。一次性事件的弱索引残项仍按
既有配额清理，不保留 inode；不把它伪装成活动 watch。

普通 byte/poll 能力持有队列与弱实例路由，不延长监视寿命。真实已接受的操作可通过
RetainedFd 取得实例租约；最后租约在其释放线程清空注册。read/poll 未增加索引锁，
也没有给普通访存增加同步。完整 OFD/FD 表、路径解析/权限与 epoll 目标身份仍待迁移。

## 验证

17 个不同 VFS 定向测试通过，包含既有关闭通知、rename/unlink、mask/oneshot、
容量、byte/poll/edge 行为，并新增：

- dup/fork/表项关闭后，真实实例租约仍能收事件；worker 最后释放后，旧能力不能
  保持监视，旧撤销能力返回 EBADF。
- 多个 worker 同时给同一通知源添加 watch 只获得一个编号，MASK_ADD 合并正确。
- 一次性 watch 被消费后，不复活旧编号；MASK_CREATE 保留 EEXIST 语义。
- 撤销通知回调可重入注册；返回时旧索引/标记已移除，替代编号仍然有效。

复用第 27 批已通过且源码/产物哈希相同的原生 Linux 对照结果。当前 runner 再验证
静态 ARM64 `inotify` 与 `worker-inotify`，各在内存/磁盘 × 1/2/4 CPU 上通过，
共 12 个 guest 配置。worker 文件通知、等待及撤销继续保持 service_requests=0。

Release CLI/FFI、五 crate/all-targets 严格 Clippy、格式和差异检查通过。
没有重新运行不受影响的 CPU/GPU/浏览器/Docker 验收，没有新增性能加速比结论。
未构建 IPA、提交或推送。

- [源码、产物与结果哈希](shared-inotify-instance-summary.json)
- 本地原始记录：`target/shared-inotify-instance-checks/`
