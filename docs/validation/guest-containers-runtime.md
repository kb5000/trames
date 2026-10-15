# 原版 crun：并发 rootfs、终端与资源控制

日期：2026-10-15，基线 `cafab11c7b78dbe1bfe6a2e3e6aad7ee626ef630`。
使用原版签名 `crun=1.23.1-r0`、Alpine 3.22.6 aarch64 minirootfs，全部容器运行在
同一 guest 内。没有修改 crun 或使用宿主容器设施代替 guest ABI。

## 验收结果

单核和四核均通过：

- cgroupfs manager 的 run/退出码 7、create/start/state/exec、TERM/stop/delete。
  修复缺少 cgroup.freeze 导致 crun 把仍在运行的容器判断为 stopped 的问题。
- pause/resume 的实际冻结、祖先冻结、迁入/迁出、阻塞 I/O、待处理信号、SIGSTOP
  状态保留和 KILL；cgroup.kill 清理所有后代，冻结状态下 force delete 成功。
- 两个完整 Alpine rootfs 同时运行，各有 PID 1、hostname、proc/sysfs、tmpfs /dev、
  私有 devpts 和 /dev/shm。暂停 alpha 后 beta 继续执行；清理后 cgroup/状态目录消失。
- 原版 crun 前台终端：三个标准 FD 均为 tty，/dev/pts/0、31×97 窗口尺寸、输出
  CONTAINER_TERMINAL_PASS 和正确的退出码 7。内部 SCM_RIGHTS 和终端 EPOLLET
  由原版运行时实际使用，不是修改后的代理程序。
- OCI pids.limit=8 实际写入 pids.max。独立 fixture 验证父子限额、fork 和线程
  名额、退出释放、迁移、降额不杀既有任务、peak/events、控制器启停和旧 FD 失效。
  两个 CPU 同时争抢最后一个名额时只有一个 fork 成功，current 不超过限额。

SCM_RIGHTS 对照宿主原生 Linux，覆盖三类 Unix socketpair、共享文件偏移、fork、
CLOEXEC、PEEK、控制区截断、辅助消息流边界及发送/接收端关闭的引用生命周期。
PTY 边缘事件对照原生 Linux/QEMU，覆盖持续可读时的新写入、排空后等长数据、
重复 wait 和 MOD/ONESHOT 重新启用。特权 devpts/cgroup 行为在 guest 验证，
没有宣称宿主 namespace 差分测试。

324 项 workspace all-feature Rust 测试、fmt、严格 Clippy、ARM64 iOS FFI 检查通过。
signalfd 25 组原生对照、原版 sudo（认证/嵌套 PTY/交互/信号）、UTS/mount/PID
namespace 与原版 BusyBox、设备/openat2、dumpable 回归通过。
系统调用入口仍为 177 implemented subset、139 missing、2 obsolete；141 个未分派
编号均返回 ENOSYS。CPU 清单未变。

## 范围与开销

这是已验收配置下的 rootful guest 容器支持。不是完整 OCI conformance 或
Docker/Podman/rootless 支持。网络共享 guest 根网络；user/IPC/net namespace、
CPU/memory/io controller、委派、seccomp、overlayfs 和 shared mount propagation
仍缺。具名 AF_UNIX、SCM_CREDENTIALS、Unix socket FD 的 in-flight 循环回收、
非终端 EPOLLET、嵌套/EXCLUSIVE epoll 仍缺，并保持明确失败。

冻结检查和 pids 计数位于 syscall/调度边界，终端数据代次在现有队列操作更新。
没有新增 CPU 指令/guest memory 热路径锁，也没有为这批功能宣称加速比。
源码采用 guest 对象实现；Linux 文档与代码用于理解 ABI，没有复制 GPL-2.0-only
内核代码进仓库。项目仍为 GPL-3.0-or-later。

## 复现

```sh
cargo build -p trames-cli --release --locked
bash scripts/build-cgroup-fixtures.sh
bash scripts/build-pty-fixtures.sh
python3 tests/integration/cgroups.py
python3 tests/integration/guest-pty.py
python3 tests/integration/containers.py
python3 tests/integration/signalfd.py
python3 tests/integration/sudo.py
python3 tests/integration/namespaces.py
python3 tests/integration/devices.py
cargo test --workspace --all-features --locked
cargo clippy --workspace --all-targets --all-features --locked -- -D warnings
cargo check -p trames-ffi --target aarch64-apple-ios --locked
python3 scripts/audit-syscalls.py
```

[日志](guest-containers-runtime.log) · [源码/产物指纹](guest-containers-runtime-data.json) ·
[原版 crun 配置、输出和 APK 指纹](guest-containers-runtime-crun.json) ·
[使用方法](../guides/guest-containers.md) · [实现边界](../architecture/guest-containers.md)
