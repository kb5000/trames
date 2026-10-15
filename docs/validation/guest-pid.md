# Guest PID namespace 验证

日期：2026-10-15。基于 `37d57e0fe8fc8ad0c05d4e11b41a2aa851472011` 的后续实现；
本记录验证 guest 内的 PID namespace，不声明完整容器运行时兼容。

## 实现范围

- CLONE_NEWPID 的父子层级、各层 PID/TID 和进程组/会话编号；
  unshare/setns 仅选择未来子进程的 namespace，拒绝祖先/兄弟回迁。
- PID 1 的普通信号保护、祖先 KILL/STOP、退出时清理后代、死 namespace fork
  返回 ENOMEM；孤儿交给同层 subreaper 或 init，外部父进程可以 wait。
- fork 返回值、parent/child TID 写入、线程及非 leader exec；wait/kill、siginfo、
  调度查询、tty group/session、记录锁 owner 的 namespace 编号。
- proc 挂载绑定创建时的当前 PID namespace；旧挂载/dirfd、bind 和 mount namespace
  复制保留绑定。进程目录、self/thread-self、task、stat/status、fd/exe/ns 按该视图工作；
  NStgid/NSpid/NSpgid/NSsid 显示从该挂载视图开始的层级。
- waitid 的 P_ALL/P_PID/P_PGID、EXITED/STOPPED/CONTINUED、NOHANG/NOWAIT/WALL；
  prctl 增加 child subreaper，保留 PR_SET_NAME/PR_GET_NAME。

## 自动验证

```sh
bash scripts/build-namespace-fixtures.sh
python3 tests/integration/namespaces.py
python3 tests/integration/fd-exec.py
TRAMES_RUNNER="$PWD/target/release/trames-run" python3 tests/integration/runtime-compat.py
TRAMES_RUNNER="$PWD/target/release/trames-run" python3 tests/integration/m6-abi.py
TRAMES_RUNNER="$PWD/target/release/trames-run" python3 tests/integration/cpu-signals.py
TRAMES_RUNNER="$PWD/target/release/trames-run" python3 tests/integration/guest-pty.py
python3 scripts/audit-syscalls.py
```

本地结果：

- 315 项 workspace all-feature Rust 测试通过；全目标全 feature Clippy `-D warnings`、
  rustfmt、ARM64 iOS FFI 编译检查通过。
- 1/4 CPU 的 UTS、chroot、mount 和 PID fixtures 通过。PID 验证覆盖嵌套、
  隐藏祖先/兄弟编号、保留的外层 proc 视图、namespace FD、线程与线程 exec、
  local/ancestor 信号发送者、STOP/CONTINUE/KILL、waitid WNOWAIT/NOHANG、
  孤儿收养、活/死 namespace setns、PID 1 清理、subreaper 和 TID 输出。
- 原版 Alpine BusyBox 的 `unshare -pf --mount-proc`、shell PID 1、后台 wait、ps
  通过；既有 UTS/mount/chroot/df/umount 命令仍通过。
- 23 项 fd-exec 原生 Linux 对照在 1/4 CPU 均通过；musl altstack/fork/thread/exec、
  I/O priority、file-size limit 回归通过；M6 event/锁/FIFO、同步 CPU 信号恢复、
  1/4 CPU PTY/控制终端对照通过。
- 系统调用清单：174 个已实现子集、142 个未实现、2 个废弃；144 个未分派编号
  均通过 ARM64 guest ELF 探测并返回 ENOSYS。本批新增 waitid 入口，prctl 扩展已有入口。

测试输出见 [日志](guest-pid.log)，源码、输入和检查日志 SHA-256 见
[证据数据](guest-pid-data.json)。PID namespace fixtures 在 guest 执行；没有把它们
描述为宿主 Linux namespace 的逐项对照。QEMU/宿主对照是上述既有 ABI 回归。

## 边界

PID namespace 本身不提供 user/capability、IPC/net、设备、cgroup 或 seccomp 隔离。
仍最多 32 个 guest 任务、每谱系 128 个存活 PID namespace、32 层嵌套；编号在会话内
不复用。proc 全局 CPU/内存统计保持 guest 范围；跨进程 root/cwd magic-link 完整能力
仍待实现。waitid P_PIDFD/非 SIGCHLD clone 类别、完整 rusage/prctl 仍缺。

映射和进程生命周期只在既有内核调度边界处理；没有为 CPU 指令、guest memory 或
原生函数增加锁。嵌套 proc 查询会增加快照过滤/编号转换；本批没有新增速度提升声明。
本地 iOS 检查不是 SDK 链接、模拟器或真机测试；未制作新的 IPA/Release。
