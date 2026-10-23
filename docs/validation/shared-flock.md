# 共享 flock 与 OFD 锁寿命

2026-10-23，第 25 批。继续拆分完整 FD/OFD 迁移所依赖的最后关闭效果。

## 变更

- 删除 Filesystem 的全局 flock map 和每次操作的全表失效引用扫描，改为 inode 内
  的共享锁集合。不同 inode 不共用锁；检查冲突、转换及提交在一次对象事务完成。
  转换失败仍释放原锁，重复申请相同类型不制造额外通知。
- 每个 OFD 延迟建立共享锁租约；dup/fork 保留同一 OFD，独立 open 有自己的租约。
  最后租约可在 worker 释放，立即删除锁并在解锁后通知，不依赖 owner 再次扫描。
  普通 FileEpoch/DirectFile 缓存只持弱能力，不延长锁寿命。
- 合格普通文件的 LOCK_NB 和 LOCK_UN 直接在 worker 共用算法、先经过 seccomp，
  计入 worker_fd_syscalls。阻塞申请仍由 owner 调度，避免先转换释放旧锁，再回退
  重放产生错误副作用；没有把 EAGAIN 送入字节 I/O 等待入口。
- 修复 O_PATH FD 错误地允许 flock：现在返回 EBADF，与原生 Linux 一致。基线全域
  fixture 可复现失败，定向 benchmark 模式排除此语义差异，比较相同控制循环。

完整 OFD 仍包含 Rc inode/socket/namespace，FD 创建/安装/close/dup 仍需 join。本项
仅迁出 flock 资源与最后释放算法，尚未迁出整个 OFD，也未迁出阻塞 flock 等待。
inode/OFD 的锁对象按首次需要建立；文件能力准备会建立弱句柄所需的租约，不能据此
声称所有普通文件负载都变快。

## 验证

40 项不同的受影响 Release Rust 检查通过：VFS 26、Linux 10、core 4。覆盖 8 路独占
竞争只有一个获胜者、最后 worker 租约释放/重入通知、缓存不延长寿命、dup/fork/关闭、
多挂载域、失败转换、O_PATH、seccomp 和阻塞转换无副作用回退；复用原 file/record-lock
算法的相关检查。一次新测试错误地假定不同 tmpfs 的内部 inode 号相等，实际已经带
域编号；修正测试假定后仅重跑该项，生产代码未再改动。

宿主原生及 ARM64 guest 在内存/磁盘后端各通过 1/2/4 CPU。真实 guest 覆盖共享 OFD、
独立 opens、fork 解锁、失败转换和 O_PATH。Release CLI/FFI、五个受影响 crate
all-targets 严格 Clippy、格式/差异检查通过。发布构建复用测试修订前的相同生产代码，
不因测试描述/断言调整重复编译或跑完整 CPU、网络、图形和应用验收。

## 定向对照

同一静态 ARM64 ELF，两线程 64 轮、共 640 次锁操作，包含共享/独占竞争、失败转换和
解锁。每种配置交错 3 次；双核/四核改后每次精确完成 640 次 worker FD 控制。

| Guest CPU | 改前中位数 | 改后中位数 | 并行批次（完整基准程序） |
| --- | ---: | ---: | --- |
| 1 | 1.001 ms | 1.023 ms | 0 → 0 |
| 2 | 26.235 ms | 1.051 ms | 643–644 → 3–4 |
| 4 | 24.009 ms | 0.501 ms | 643 → 3–4 |

单核没有测出改善。这个短样本受宿主调度影响；两版 service_requests 都为 0，主要
减少的是全体返回/重建。Linux x86_64 上的控制密集负载不能替代 iPad/CoreMark 或
应用总体加速比。

证据：[shared-flock-summary.json](shared-flock-summary.json)。原始产物在
`target/shared-flock-checks/`。没有提交、发布或构建 IPA。
