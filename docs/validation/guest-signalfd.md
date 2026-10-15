# Guest signalfd 与原版 crun 生命周期

日期：2026-10-15。基线：`e42637f1f52436bcfec0c808cfa481622fa2cb0e`。
本批增加 signalfd4（74），将信号来源拆为线程私有与进程共享队列，并打通原版
签名 Alpine `crun=1.23.1-r0` 的选定容器生命周期。没有修改应用版本或手动发起 IPA 构建。

## 已验证

1/4 CPU 各运行 **25 组宿主原生 Linux 对照**，guest 使用独立交叉编译的静态
AArch64 musl ELF，宿主对照使用原生 C 编译器：

- 参数、fd 类型、mask 更新、NONBLOCK/CLOEXEC、SIGKILL/SIGSTOP 去除、readlink/fdinfo。
- 标准信号合并、pending 位、实时号码优先级和不同发送进程的 FIFO 顺序；私有队列
  先于共享队列，Linux 同步类号码优先级，SI_USER/SI_TKILL、发送者 PID/UID。
- 完整记录、跨 iovec、非整倍数长度、EFAULT 消费及部分成功。
- dup 共享 mask/status flags；epoll level/ONESHOT、MOD 重置、dup 持有后的最后关闭。
- pselect、阻塞 read/ppoll/epoll、EINTR、SA_RESTART、临时信号 mask 恢复。
- fork 后读取子进程自己的信号，exec 保留 pending 并关闭 CLOEXEC，SIGCHLD 状态与
  wait 共存，跨线程共享/私有消费，普通 handler/sigtimedwait/FD 关闭后的信号消费。
- 被阻塞的 SIG_IGN 信号、disposition 变更丢弃 pending、POSIX timer 的 value/overrun 字段。

单元测试还检查实时队列预算不挤掉 SIGKILL，以及线程定向 stop/continue 的整个
线程组状态变化。同步 CPU fault 保留精确地址与上下文，不被旧的异步同号信号覆盖。

原版 crun 在 **1/4 CPU 均通过**：

- 实际 BusyBox shell payload 输出 `CONTAINER_PAYLOAD`，退出码准确传回 7。
- payload 是 namespace PID 1；读取 proc、独立 UTS hostname；父 UTS 和 mount 视图保持独立。
- create → created，start → running，exec 在既有容器中运行第二个命令。
- TERM 到达 PID 1 的 shell trap，写出清理标记；state → stopped，delete 清理状态目录。
- 输入来自签名 APK，未修改 crun 或替换它的启动逻辑。完整 stdout/stderr、配置和包 SHA-256
  保留在证据中。`open_tree`、`keyctl`、`pidfd_open` 等 ENOSYS 由原版运行时自行回退。

扩展 `/proc` 挂载测试时发现 mount 的 proc FD 快照复用问题：现在挂载入口刷新 proc，
并用 FD 的 mount/inode 定位目标。新增 fixture 覆盖 FD 号码复用、目标改名和同名重建，
确认新挂载只出现在实际被引用目录。命名空间 fixture 在 1/4 CPU 通过。

322 项 workspace all-feature Rust 测试、严格 Clippy、fmt 和 ARM64 iOS FFI 编译检查
通过；runtime-compat、CPU fault、M6 event/IPC/lock/FIFO、namespace/BusyBox、PTY、
原版 sudo、cgroup、设备/openat2、fd-exec、capability/原版 capsh 回归通过。
设备、挂载特权行为在 guest 验证，没有宣称宿主特权 namespace 差分。

## 范围

这批 crun 自动验收显式使用 `--cgroup-manager disabled`，private PID/mount/UTS、
一个 proc 挂载、非终端静态 BusyBox rootfs、NNP 和选定 capability 集合。cgroupfs/
systemd manager、资源控制、多个并发容器、私有 devpts、完整 OCI、Docker/Podman
不在通过范围。后续优先扩大这些真实运行时场景，再补 user/IPC/net namespace。

信号队列固定为 4096 条预算，标准信号不受实时队列耗尽影响；这不是 Linux 每 UID
的 RLIMIT_SIGPENDING。rt_sigqueueinfo/rt_tgsigqueueinfo 尚无入口；SIGCHLD CPU 时间
仍为零，完整异步 I/O 信号来源未实现。Linux 跨 fork 继承 epoll 的原注册信号等待
队列绑定未模拟，子进程应重新创建并注册 epoll；EPOLLET/EXCLUSIVE/嵌套 epoll 仍缺。

没有增加 CPU/native-memory 热路径锁，也没有性能跑分或新增加速比主张。
行为依据 [Linux v6.12 signalfd](https://github.com/torvalds/linux/blob/v6.12/fs/signalfd.c)
与 [signal queues](https://github.com/torvalds/linux/blob/v6.12/kernel/signal.c)，通过
独立 ABI 对照校验；实现是针对 guest 对象的 Rust 代码，没有调用宿主 signalfd。

## 复现与证据

```sh
cargo test --workspace --all-features --locked
cargo clippy --workspace --all-targets --all-features --locked -- -D warnings
cargo check -p trames-ffi --target aarch64-apple-ios --locked
cargo build -p trames-cli --release --locked
bash scripts/build-signalfd-fixtures.sh
python3 tests/integration/signalfd.py
bash scripts/build-namespace-fixtures.sh
python3 tests/integration/namespaces.py
python3 tests/integration/containers.py
python3 scripts/audit-syscalls.py
```

清单更新为 **177 个 implemented subset、139 个 missing、2 个 obsolete**；
全部 141 个未分派编号返回 ENOSYS。CPU 指令清单不变。

[验证日志](guest-signalfd.log) · [源码/二进制指纹](guest-signalfd-data.json) ·
[原版 crun 配置、输出与包指纹](guest-signalfd-crun.json) ·
[当前实现范围](../architecture/guest-containers.md)
