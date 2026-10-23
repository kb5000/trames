# 共享 inotify 与被监视文件的 worker I/O

2026-10-23，第 21 批。完整协调器拆除仍在进行。

## 实现与边界

- 事件字节、watch mask、one-shot 消费、合并/溢出、可读字节数和边沿 epoch
  收敛到每个 inotify 实例的共享队列。串行入口与 worker 使用同一套算法；多字段
  状态在实例锁内提交，订阅唤醒在解锁后发生，不引入服务线程或轮询总线。
- read/readv、FIONREAD、ppoll/epoll 等待和 inotify_rm_watch 可在 worker 执行。
  等待交还执行槽，生产者直接通知运行队列；rm_watch 的 IN_IGNORED 与失效一起提交。
- 被监视普通文件的短读和受限原位写直接提交 ACCESS/MODIFY，无须仅为通知回到 owner。
  EOF、零长度、失败及尚未执行的增长回退不产生通知。没有监视者的快路径不分配通知。
- 路径/link 名称在 FileEpoch 内冻结，通知只保留弱队列引用和单调 watch ID。
  旧能力不能向替换 watch 发事件；普通字节/poll 能力不延长监视注册寿命。
  owner 的弱 inode 元数据和节点标记有清理界限，反复 worker rm_watch 不耗尽 watch 配额。
- 宿主 Linux 对照补正 readv 语义：每个非空 iovec 必须容纳完整事件，不能先合并两个
  短 iovec 再拆记录；已有部分进展时保留已返回字节，下一条事件留在队列。串行/worker
  共用原 readv 算法，io_uring 保留现有兼容层的缓冲语义。

inotify_init/add_watch 的 FD 安装、路径解析/注册、最后 OFD 关闭仍有 owner 元数据。
文件增长、同步、配额、映射冲突和大块磁盘读取的原边界保留。FD/OFD 主体与动态查表
尚未跨 worker 共享，不能把这次队列迁移等同于 close/dup 或整个协调器已经迁完。

## 验证

122 项不同的受影响 Release Rust 检查通过（复用本轮未受后续修改影响的通过项）：
VFS 31、Linux 86、core 5。新增/调整覆盖并发 one-shot、完整事件/计数、解锁后回调、
边沿/ONESHOT、watch ID 替换、最后关闭、8193 次撤销再注册、EOF/零长度/增长回退。
io_uring 专项覆盖等待时关闭并复用数值 FD，保留原监视对象，由文件 worker 唤醒且只消费一次。

宿主原生与同一份 ARM64 ELF 的完整 inotify 案例一致；内存/磁盘后端各通过 1/2/4 CPU。
原 worker io_uring ABI 在 1/2/4 CPU 通过。Release CLI/FFI 和五个受影响 crate 的
all-targets 严格 Clippy 通过。没有重复完整 CPU、Firefox、Docker 或 GPU 验收。

## 定向对照

`worker-inotify.c`：两个线程进行 64 轮原位写/读取通知，读取交替使用阻塞 read 和
边沿 epoll/FIONREAD/readv，最后在 peer 仍存活时撤销 watch。新旧 Release runner
使用相同静态 ARM64 ELF，每个 CPU 配置交错 3 次，计时循环中位数：

| Guest CPU | 改前 | 改后 |
| --- | ---: | ---: |
| 1 | 0.295 ms | 0.277 ms |
| 2 | 8.924 ms | 0.790 ms |
| 4 | 4.493 ms | 0.415 ms |

双核/四核改后每次有 64 次 worker 文件调用、32 次 worker poll 和 1 次 worker watch
撤销；服务请求为 0。完整程序仍有线程退出/关闭引起的少量并行批次，并非零协调。
该循环很短，计时有宿主调度波动；核心证据是服务转交和并行批次的大幅减少。
这些是 Linux x86_64 测试机上的通知密集负载数据，不代表 iPad、CoreMark 或应用总体加速。

机器可读结果与源码/产物哈希：[worker-inotify-summary.json](worker-inotify-summary.json)。
原始输出保留在 `target/worker-inotify-checks/`。本次未提交、发布或构建 IPA。
