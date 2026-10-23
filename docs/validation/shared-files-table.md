# 共享 Files 表：移除描述符传播

2026-10-23。接在共享管道端点之后，原有 20 批 worker 调用迁移之外的一次所有权改造。
完整协调器拆除仍在进行，本次没有把 close/dup 计入已迁出的 worker 系统调用。

## 实现

- CLONE_FILES 指向同一个表对象。删除协调器 `ResourceChanges.files`、每次 syscall
  对 FD 版本的跟踪，以及变更后逐任务 `share_descriptors_from` 的 map 传播。
- Vfs/Linux 显式选择 fork 或共享表；共享克隆不再先复制临时 map 和 CLOEXEC 单元。
  fork/unshare 仍复制表项与 CLOEXEC，保持共享 OFD；现有字节/文件能力缓存仍可复用。
- 查询返回持有原 OFD 的描述符引用，表借用在资源操作前结束。关闭和数值复用不会让
  已取得的引用访问新的文件。普通字节/poll 快照仍不额外持有管道读写端。
- dup3 一次发布替换后的表项，再在表借用之外释放旧描述符的 POSIX 锁和关闭效果。
  不再先 close 目标再 insert，避免关闭通知期间出现中间空槽。
- 删除 Process.files_id。退出/exec 依实际表引用关系释放；非最后一个任务只放下
  表引用，最后一个任务才关闭表项，不再遍历进程表确认文件表是否仍被共享。

表目前仍是 owner 内的 `Rc<RefCell<TableState>>`，OFD 仍有 Rc/namespace/inotify 等
依赖；**没有把它标成 Send，也没有用整个 VFS 的锁跨线程共享**。worker 当前看到的
仍是冻结能力快照，close/dup/open 等结构操作仍需结束并行批次。这次去掉的是表副本
和协调传播，后续要将真实 OFD/端点所有权迁出，再把动态查询和等待引用接入 worker。

## 验证

327 项 Release Rust 检查通过：VFS 227、Linux 的 direct/io_uring/record-lock 85、
core 受影响的 clone/exec/控制回调/FD/管道/io_uring 15。4 个新增案例覆盖：

- 32 个共享表引用不增加每个 OFD 的引用数量，表修改立即可见且 fork 隔离。
- 关闭/数值复用后的原始 I/O 引用、EOF 和 io_uring 私有 FD 视图。
- dup3 替换先于旧端点释放，关闭后可再次访问表。
- 任务放下表引用时保留共享端点，最后关闭产生 EOF。

复用源码未变的 ARM64 产物，FD 控制与 fork/unshare/成功和失败 exec、POSIX 锁、
FIFO、worker io_uring 分别在 1/2/4 CPU 通过；C 语义与宿主对照一致。
Release CLI/FFI 构建及五个受影响 crate 的 all-targets 严格 Clippy 通过。

## 定向对照

新增 `shared-files-table.c`：8 个 reader 任务共享表，96 个 FD 别名，父任务执行
1024 次 dup/close 表项变更；最后唤醒 reader 并核验 EOF。新旧 runner 使用同一份
静态 ARM64 ELF，每个 CPU 配置交错运行 3 次，以下为计时循环中位数：

| Guest CPU | 改前 | 改后 |
| --- | ---: | ---: |
| 1 | 14.49 ms | 4.11 ms |
| 2 | 83.45 ms | 59.66 ms |
| 4 | 86.63 ms | 85.11 ms |

这是 Linux x86_64 测试宿主上的特定 FD 表负载，不是 iPad、CoreMark 或应用整体提速。
四核样本仍有明显波动，不能据此宣称四核提升。结构操作仍造成约 1170 次并行批次，
保留了大量等待/准备成本；下一步必须迁移真实 FD/OFD 操作，而非继续复制快照。

记录和哈希：[shared-files-table-summary.json](shared-files-table-summary.json)。
原始输出位于 `target/shared-files-table-checks/`。本次未重复完整 CPU、Firefox、
Docker 和 GPU 验收，也未构建或发布 IPA。
