# 共享 PTY 从端的实际引用寿命

2026-10-23，第 30 批协调器拆分。PTY 从端的真实打开引用已独立为
`Arc<PtySlaveEndpoint>`，最后引用可在 worker 直接递减共享打开计数并通知主端。
普通字节/轮询快照只持有 PtyIo，不被计为打开引用。机器记录见
[shared-pty-slave-summary.json](shared-pty-slave-summary.json)。

## 行为与剩余边界

dup/fork 和显式保留引用共享同一个从端，独立 open 才增加计数；关闭数字 FD 或
重用该编号，不会提前关闭仍在执行操作的原端点。最后引用释放不获取 inode、mount
或 terminal 锁，通知可以立即重入终端读取/轮询。计数沿用 relaxed 原子，不新增
普通访存屏障、服务线程或轮询。普通最终关闭保留元数据投影直到关闭通知完成。

关闭最后从端后，主端看到 HUP；已缓冲输出仍可读完，然后返回 EIO。主端保持打开
时，devpts 从端节点仍存在，重新打开后取消 HUP。主端关闭后的从端仍读到 EOF、
写入返回 EIO，devpts 节点按原路径删除。

本批没有迁移主端最终关闭：它还涉及 devpts 目录、终端会话/进程组及 SIGHUP/
SIGCONT。完整 FD/OFD 和这些共享域仍需后续改造，没有让 Rc 的 PtyEnd 跨线程，
也没有把终端强行纳入目前不具备进程/终端权限的 io_uring 入口。新增一次真实打开
的 Arc 对象，不改变字节读写的锁或查表路径；不据此声称整体性能提高。

## 验证

15 项不同的 release Rust 检查通过：VFS 12、Linux 1、Core 2。新增检查覆盖
FD 别名/fork/重用、worker 最后释放后的直接 HUP 唤醒、通知中重入读取、独立打开
与并发别名释放、缓冲输出不丢失，以及主端先关闭的 EOF/EIO 和节点删除。既有
devpts 隔离/bind/懒卸载、原终端 ABI、前后台权限和宿主键盘/尺寸信号检查通过。

一项新测试最初错误地从仅做字节分派的 Vfs.write 入口写 PTY，缺少终端访问上下文；
修正为 DirectFd 的终端入口后，仅重跑该失败项。没有改动生产算法或重跑其他已通过项。

原 ARM64 fixture、C 源码及重新生成的宿主 oracle 与第 12 批记录同哈希。
测试脚本增加 `--reuse-oracles`，核对哈希后复用八份已通过的原生参考输出。
新 release runner 跑过 read/vector/epoll/ppoll 四种模式，各覆盖 1/2/4 CPU
以及四 CPU 中固定同一执行槽的情况，共 16 组。全部输出一致、服务转交请求为 0；
多核继续由原 worker 执行 I/O 并让出等待槽，没有重跑上一批 TCP 或其他无关项目。

Release CLI/FFI、三个相关 crate 的严格 release Clippy（含测试）、格式/差分和
脚本语法检查通过。生产代码在上述测试修正时没有变化，复用已完成构建。
原始记录在本地 `target/shared-pty-slave-checks/`，没有制作 IPA、提交或发布。
当前为 Linux x86_64 验证，iPad 仍待后续验收。

```sh
python3 tests/integration/worker-pty.py \
  --runner target/perf-runtime/release/trames-run \
  --oracle target/shared-pty-slave-checks/native-worker-pty \
  --reuse-oracles target/worker-pty-checks/comparison \
  --output target/shared-pty-slave-checks/guest-recheck
```
