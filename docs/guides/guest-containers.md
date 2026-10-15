# 在 Trames guest 内运行 Alpine 容器

这套用法使用未经修改的 Alpine `crun`。当前验收范围是 rootful 容器，共享本
Trames guest 的直连网络，隔离 PID、主机名、挂载、临时文件和 PTY，并支持
cgroupfs 的暂停/恢复、终止和 pids 限额。需要包含本批实现的新构建；旧 IPA
不会因下载这个 JSON 自动获得新内核功能。

## 准备 rootfs 和配置

在 Trames 终端执行：

```sh
apk add crun
mkdir -p /root/containers/alpine/rootfs
cd /root/containers/alpine
wget https://dl-cdn.alpinelinux.org/alpine/v3.22/releases/aarch64/alpine-minirootfs-3.22.6-aarch64.tar.gz
printf '%s\n' '821565fa8f3953eefd12497b166b4b50add2f7c57fb312e75862f5867e06fefe  alpine-minirootfs-3.22.6-aarch64.tar.gz' | sha256sum -c -
tar -xzf alpine-minirootfs-3.22.6-aarch64.tar.gz -C rootfs
wget -O config.json https://raw.githubusercontent.com/kb5000/trames/main/examples/containers/alpine.json
cp /etc/resolv.conf rootfs/etc/resolv.conf
crun --root /run/crun --cgroup-manager cgroupfs run alpine
```

随后进入容器内的 shell。`exit` 退出并由 crun 清理运行状态；rootfs 中的普通
文件保留，`/dev`、`/dev/pts`、`/dev/shm` 的挂载内容随容器释放。JSON 是传给
原版 crun 的普通 OCI 配置，Trames 没有另写一个 OCI 启动器。

示例使用 8 个任务的 pids 限额，包括所有线程。可修改 `linux.resources.pids.limit`，
但整个 Trames 会话目前总计仍最多 256 个任务，运行时和外部 shell 也占名额。
两个 bundle 应使用不同的容器 ID、`linux.cgroupsPath`、hostname 和 rootfs 路径。

## 生命周期

对正在运行的容器，可从同一个 guest 的另一个 shell 执行：

```sh
crun --root /run/crun list
crun --root /run/crun state alpine
crun --root /run/crun exec alpine /bin/sh -c 'echo inside; cat /proc/1/status'
crun --root /run/crun pause alpine
crun --root /run/crun resume alpine
crun --root /run/crun kill alpine TERM
crun --root /run/crun delete --force alpine
```

PID 1 遵循 Linux 的信号处理规则：没有 TERM handler 的程序可能忽略 TERM，
需要正常退出或强制删除。暂停期间不执行 guest 指令；待处理普通信号在恢复后
交付，KILL/force delete 可以终止冻结任务。

`create`/`start`/`exec`/`delete` 已由自动测试覆盖；后台工作负载应将配置中的
`process.terminal` 设为 false。当前不支持外部 `--console-socket`，前台 `run`
可以直接使用 app 终端，包含窗口尺寸和终端输入输出。

## 当前边界

- 网络共享当前 guest 的直连网络和端口空间；没有给容器建立虚构的独立 IP。
- 尚未提供 user/IPC/net namespace、rootless、seccomp、CPU/memory/io 配额、
  overlayfs、共享挂载传播或完整 Docker/Podman 支持。
- 原版 `crun spec` 的默认配置包含更多设施，当前应从上面的已验收配置开始。
- 这不是运行不可信程序的完整安全隔离边界。

具体实现与语义见[架构说明](../architecture/guest-containers.md)，复现及证据见
[容器运行验证](../validation/guest-containers-runtime.md)。
