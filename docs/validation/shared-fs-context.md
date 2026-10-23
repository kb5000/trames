# 共享 fs_struct 与 worker umask

2026-10-23，第 22 批。接在共享 inotify 之后，继续删除协调器的状态传播。

## 变更

- CLONE_FS 任务直接持有同一个根目录、当前目录和 mount 引用对象；fork 复制当前
  上下文。删除 ResourceChanges.fs、每次 syscall 的 fs_context 快照/比较、逐任务
  share_context_from，以及 Process.fs_id。宿主 control 的部分成功修改也立即可见，
  无需先移出根任务、同步其他任务再放回。
- FS 与 Files 作为独立的克隆参数。unshare(CLONE_FILES) 不分离 FS，unshare(CLONE_FS)
  不分离 Files；NEWNS 对 FS 的分离/挂载重映射保持一致。mount setns 仍按实际活跃
  任务的共享对象关系检查，pivot_root 仍更新同一 mount namespace 中受影响的独立 FS。
- umask 从复制字段改成独立共享原子，owner/worker 共用一次 relaxed swap，限制到
  9 位并返回原值。不为普通 guest 内存增加屏障；不保留路径 Rc 到 worker，不加全局锁。
- 宿主 Linux 对照确认普通 exec 保留 CLONE_FS（成功与失败均如此）。旧 fs_id 在成功
  exec 时重置会错误地分离 cwd/umask；新实现修正这一点，文件表的 close-on-exec
  私有化保持原行为。基线可复现该失败，当前 guest 与原生一致。

路径、mount 与 inode 本体仍是 owner 内的 Rc/RefCell，路径调用仍需结束并行批次。
新 FsContext 没有被标记为 Send。该项消除了 FS 状态复制，并迁出 umask；完整 FD/OFD、
VM 与 task 生命周期仍未完成迁移，heap 和 signal actions 仍有 owner 传播。

## 验证

134 项不同的受影响 Release Rust 检查通过：VFS 84、Linux 21、core 29。
覆盖共享/复制/脱离上下文、目录 FD、chroot、挂载重映射、权限和存储路径、共享 Files
独立性、宿主 control 部分失败、失败 exec 和 worker seccomp。并发 umask 测试核验
每次交换的前值恰好出现一次，不假定线程获胜次序。

`shared-fs-context.c` 的宿主原生与 guest 在内存/磁盘后端各通过 1/2/4 CPU：
CLONE_VM × CLONE_FS 四种组合、成功/失败 exec、并发交换。原 FD table 域 fixture
仅重跑 domains 部分，1/2/4 CPU 通过；无需重复完整控制循环、CPU、网络或图形测试。
Release CLI/FFI、五个受影响 crate all-targets 严格 Clippy、格式与差异检查通过。

修正了一个旧 futex 单测的过期预期：shared-key wake 现在已能在 worker 执行；测试
清除前一步故意设置的非法 count 后验证结果 0。没有改动 futex 生产实现。

## 定向对照

同一静态 ARM64 ELF，两个线程共 256 次 umask 原子交换。基准模式单独比较相同循环，
避免把旧版本 exec 语义失败混入耗时；完整域语义另外验证。每个 CPU 配置交错 3 次：

| Guest CPU | 改前中位数 | 改后中位数 | 并行批次（完整基准程序） |
| --- | ---: | ---: | --- |
| 1 | 0.179 ms | 0.167 ms | 0 → 0 |
| 2 | 7.439 ms | 0.142 ms | 258–259 → 3 |
| 4 | 7.433 ms | 0.116 ms | 257–258 → 3–4 |

两版 service_requests 均为 0；这里减少的是整组返回/重建，进一步说明这个计数为 0
不能代表协调器已经撤掉。worker_syscalls 还包含 yield/时钟等调用，不当成精确 umask
计数。计时循环很短、有宿主调度波动，只证明该控制调用负载的改进，不代表 CoreMark、
iPad 或整个应用的加速比。

证据和哈希：[shared-fs-context-summary.json](shared-fs-context-summary.json)。
原始产物：`target/shared-fs-context-checks/`。本次未提交、发布或构建 IPA。
