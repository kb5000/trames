# 原版 Docker：守护进程、镜像和容器创建

日期：2026-10-15；基线 `77d20ce53e76a510342148b40ac029777550e503`。
原版签名 Alpine Docker 28.3.3-r5、containerd 2.1.5-r2、crun 1.23.1-r0；
Alpine 3.22.6 aarch64 minirootfs。所有引擎进程运行在 guest 内，源码未修改。
项目新增代码仍为 GPL-3.0-or-later。

## 本批实现

- filesystem/abstract AF_UNIX stream/seqpacket：bind/listen/connect/accept4、名称、
  SO_PEERCRED、DAC/umask、backlog、过期 inode、关闭与 unlink 生命周期。
  命名 socket inode 持久化，但端点状态不跨内核会话恢复。
- Unix socket、eventfd、pipe/FIFO、Netlink 回复队列的 EPOLLET 代次；常规文件
  返回 EPERM。accept 按实际地址长度预检，短地址位于可写页边界时不误报 EFAULT。
- PR_SET/GET_PDEATHSIG：创建线程退出、发送者身份、fork 清零、普通 exec 保留、
  effective/fs UID/GID 变化清零。线程总额度 256，proc/rlimit 一致；主 ELF 输入
  上限 256 MiB，映射额度仍为每地址空间 512 MiB / 全会话 1 GiB。
- `utimensat(AT_SYMLINK_NOFOLLOW)` 正确更新 dangling symlink；12 个 xattr 接口
  持久保存受限 `user.*`，覆盖 DAC、readonly、CREATE/REPLACE、hardlink/unlink、
  元数据预算和 checkpoint 失败回滚。security.capability/ACL 不伪造成功。
- 当前根网络的 ns/net 句柄、task 路径、nsfs bind 和同网络 setns。没有创建独立
  网络 namespace；Netlink 的 lo-UP 仅确认已经生效的状态并检查 CAP_NET_ADMIN。
  私有挂载收到 make-slave 仍保持 private，没有假装存在 shared master。
- FIFO 经 O_PATH/proc FD 重开时建立正确的新端点，修复 Docker/containerd
  日志流的 `read /proc/self/fd/N: illegal seek`。

## 验收范围

全新磁盘，分别使用 1 和 4 个 vCPU、`GOMAXPROCS=2`、根网络 native Full。
原版 apk 验证包签名；原版 containerd 单独启动，再由 dockerd 连接，storage=vfs、
runtime=crun，关闭 bridge/iptables。`/run` 和 `/dev/shm` 使用普通 guest tmpfs。

通过 `docker version/info`、完整 Alpine tar 的 `docker import`、image inspect、
create、状态 created、inspect、rm 和 image rm。测试保留了配置、APK/rootfs
SHA-256、命令输出及各阶段布尔结果，没有用宿主 Docker 代替。

**实际容器执行仍未通过。** `docker start -a` 已进入 containerd shim，随后失败：

```text
failed to adjust OOM score for shim: get parent OOM score:
open /proc/<parent>/oom_score_adj: no such file or directory
```

`DOCKER_RUN_BLOCKED` 与通过的 daemon/image/create/cleanup 分开记录。
`python3 tests/integration/docker.py --require-run` 会要求实际 payload 和退出码 7，
当前会失败；CI 的测试名称和日志明确说明该诊断范围。下一步是实现进程 OOM
属性及相应权限/生命周期，再继续追踪 shim → crun → payload，不把当前结果标为
完整 Docker 兼容。

332 项 workspace all-feature Rust 测试、严格 Clippy、fmt、ARM64 iOS FFI 编译通过。
Unix IPC、ET、xattr、FIFO 与父线程退出信号在 1/4 CPU 下对照宿主原生 Linux；
新增 C fixture 通过 `_FORTIFY_SOURCE=3` / `-Werror` 编译。
原版 crun 的并发完整 Alpine rootfs、cgroupfs/pids/freezer、前台 PTY 与生命周期
回归通过；UTS/mount/PID namespace、原版 BusyBox、M6 ABI、PTY/SCM_RIGHTS 回归通过。
特权 namespace/nsfs bind 在 guest 中验证，不声称本地宿主特权差分测试。

当前系统调用审计为 **189 个实现子集、127 个缺失、2 个废弃**；所有 129 个未分派
编号返回 ENOSYS。CPU 指令清单未变，没有新增 CPU/native-memory 热路径锁或加速比。

## 持久化与限制

manifest v4 保存属性，仍可读取 v3 的空属性表并在下一次 checkpoint 升级。
旧二进制不能读取 v4；数据格式和配额见[存储架构](../architecture/storage.md)。

当前不支持独立 user/IPC/net namespace、overlayfs、共享挂载传播、CPU/memory/io
控制器、seccomp、完整 inotify、AF_UNIX socket FD 循环回收/SCM_CREDENTIALS。
普通容器配置的后续阻塞还可能涉及这些设施；未验收 Registry pull、BuildKit
构建、Compose、rootless、Docker 交互终端或 Docker 容器网络服务。
原生 TCP/UDP 的 EPOLLET 仍缺；具名 datagram 定址通信仍缺。

源码行为参考 Docker v28.3.3 的 daemon/osl/archive、Go 与 containerd fifo 的
原始实现；没有修改这些上游程序来绕过验证，也没有复制 GPL-2.0-only 内核代码。
独立原版 crun 的已支持运行范围保持可用。

## 复现及证据

```sh
cargo build -p trames-cli --release --locked
bash scripts/build-unix-fixtures.sh
python3 tests/integration/unix-sockets.py
python3 tests/integration/docker.py
python3 tests/integration/containers.py
python3 tests/integration/namespaces.py
python3 tests/integration/m6-abi.py
python3 tests/integration/guest-pty.py
cargo test --workspace --all-features --locked
cargo clippy --workspace --all-targets --all-features --locked -- -D warnings
cargo check -p trames-core -p trames-distro -p trames-ffi --target aarch64-apple-ios --locked
python3 scripts/audit-syscalls.py
```

[配置/输入/实际输出](guest-docker-results.json) · [源码/产物指纹](guest-docker-data.json) ·
[ABI 回归日志](guest-docker.log) · [配置指南](../guides/guest-docker.md) ·
[架构](../architecture/guest-containers.md)
