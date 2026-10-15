# Guest Docker：守护进程、镜像与创建阶段

当前开发版支持原版 Alpine Docker/containerd 的 Unix socket 通信、守护进程启动、
`docker version/info`、vfs 镜像导入和容器创建/查询/删除。**容器启动尚未验收**：
containerd shim 在读取 `/proc/PID/oom_score_adj` 时失败，实际 payload 尚未执行。
需要运行容器时，已有可用范围见[原版 crun 入门](guest-containers.md)。

这批没有升级 App 版本或手动生成 IPA；此前安装包不自动包含这些改动。

## 配置

在 guest 内安装 `docker-engine docker-cli containerd crun`。测试使用 Alpine
3.22.6 aarch64、Docker 28.3.3-r5、containerd 2.1.5-r2、crun 1.23.1-r0，APK 仍由
原版 apk 验签。不要使用宿主 Docker socket。

将仓库的 [daemon.json](../../examples/docker/daemon.json) 和
[containerd.toml](../../examples/docker/containerd.toml) 分别放进 guest 的
`/etc/docker/daemon.json`、`/etc/containerd/config.toml`。
使用 `vfs` 存储和 guest 根网络，关闭 bridge/iptables；独立网络隔离和 overlayfs
尚缺。vfs 会复制镜像层，文件操作和空间开销较大，目前不宣称 Docker 性能优化。

新启动的 guest 会话应给 `/run` 和 `/dev/shm` 挂载 tmpfs（已由初始化系统挂载时
不要重复挂载）。测试配置为 `/run: mode=755`、`/dev/shm: size=16m,mode=1777`。
`/run` 中的 PID/socket 文件属于当前内核会话，不应从持久磁盘当作活跃进程恢复。

```sh
mkdir -p /run /dev/shm
mount -t tmpfs -o mode=755 tmpfs /run
mount -t tmpfs -o size=16m,mode=1777 tmpfs /dev/shm
export GOMAXPROCS=2
containerd --config /etc/containerd/config.toml >/tmp/containerd.log 2>&1 &
```

等日志出现 `containerd successfully booted` 后再启动 Docker：

```sh
dockerd --config-file /etc/docker/daemon.json >/tmp/dockerd.log 2>&1 &
```

等 `API listen on /var/run/docker.sock` 后，可使用 `docker version`、`docker info`。
先单独启动 containerd，可避开 dockerd 内置的固定启动期限；解释执行下冷启动
时间会受宿主负载影响。`GOMAXPROCS=2` 是当前测试设置，不改变 Trames 的 vCPU 数。

## 已验证的镜像和容器元数据操作

将官方 Alpine minirootfs 的 tar 导入 guest，然后执行：

```sh
docker import /tmp/alpine-rootfs.tar trames-alpine:probe
docker image inspect trames-alpine:probe
docker create --name smoke --network host --ipc host --cgroupns host \
  --security-opt seccomp=unconfined trames-alpine:probe /bin/sh -ec 'echo hello'
docker inspect smoke
docker rm smoke
docker image rm trames-alpine:probe
```

`--network host` 指当前 Trames guest 的根网络。容器没有独立网络 namespace；
后续端口监听仍沿用 guest 的宿主直连设计。`--ipc host`、`--cgroupns host` 和
关闭 seccomp 是当前选定范围，不构成完整 Docker 隔离配置。

`docker start -a smoke` 是后续启动诊断入口，目前仍被 shim 的 proc OOM 接口阻塞。
Registry pull、build/buildkit、Compose、rootless、容器 HTTP 服务与交互终端尚未
通过 Docker 验收。CDI 的 inotify、CPU/memory/io 控制器等警告仍可能出现。

## 自动验证

```sh
python3 tests/integration/docker.py
```

测试使用新磁盘和签名 APK，分别验证 1/4 CPU 的 daemon、image、create/inspect、
remove；启动诊断单独记录 `DOCKER_RUN_BLOCKED`，不会被解释为容器运行成功。
`--require-run` 会要求实际 payload 与退出码通过，目前应失败。
完整日志、配置和输入指纹见[验证记录](../validation/guest-docker.md)。
