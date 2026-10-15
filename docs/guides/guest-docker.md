# 在 guest 内运行 Docker 容器

当前开发版可通过原版 Alpine Docker → containerd → crun 执行 Alpine 容器：
镜像导入、create/start、PID 1 payload、退出码、docker logs 与清理均纳入验收。
使用 vfs、共享 guest 网络/IPC/cgroup namespace、关闭 seccomp 的选定配置，
保留默认设备过滤，**无需 `--privileged`**。这不是完整 Docker 隔离或功能覆盖。

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
export GOMAXPROCS=1
containerd --config /etc/containerd/config.toml >/tmp/containerd.log 2>&1 &
```

等日志出现 `containerd successfully booted` 后再启动 Docker：

```sh
dockerd --config-file /etc/docker/daemon.json >/tmp/dockerd.log 2>&1 &
```

等 `API listen on /var/run/docker.sock` 后，可使用 `docker version`、`docker info`。
先单独启动 containerd，可避开 dockerd 内置的固定启动期限；解释执行下冷启动
时间会受宿主负载影响。`GOMAXPROCS=1` 是当前测试设置，不改变 Trames 的 vCPU 数。

## 运行容器

将官方 Alpine minirootfs 的 tar 导入 guest，然后执行：

```sh
docker import /tmp/alpine-rootfs.tar trames-alpine:probe
docker image inspect trames-alpine:probe
docker create --ulimit nofile=256:256 --name smoke --network host --ipc host --cgroupns host \
  --security-opt seccomp=unconfined trames-alpine:probe /bin/sh -ec 'echo hello'
docker start -a smoke
docker inspect smoke --format '{{.State.ExitCode}}'
docker logs smoke
docker rm smoke
docker image rm trames-alpine:probe
```

`--network host` 指当前 Trames guest 的根网络。容器没有独立网络 namespace；
后续端口监听仍沿用 guest 的宿主直连设计。`--ipc host`、`--cgroupns host` 和
关闭 seccomp 是当前选定范围，不构成完整 Docker 隔离配置。

`--ulimit nofile=256:256` 与当前 guest FD 表上限一致。shim 所需的
`oom_score_adj`、pipe fchown/fchmod 和 cgroup device BPF 已实现；设备规则真正
限制新 open/mknod，容器内既有 FD 保持原有访问能力。

冷启动仍较慢，尚未做性能优化。Registry pull、build/buildkit、Compose、rootless、
Docker 容器 HTTP 服务和交互终端尚未验收。CDI/cgroup 监控的 inotify、pidfd、
CPU/memory/io 控制器等仍可能产生提示。`io.max` 仅允许空配置，限速写入失败；
`/dev/mqueue` 只有可挂载的空视图，POSIX 消息队列系统调用尚未实现。

## 自动验证

```sh
python3 tests/integration/docker.py --require-run
```

测试使用新磁盘和签名 APK，分别验证 1/4 CPU 的 daemon、image、create/start、
PID 1 payload、退出码 7、日志、remove。CI 使用 `--require-run`，启动失败不能
通过检查；省略该参数只用于单独记录阻塞的诊断。测试配置未使用 GODEBUG 改写
Go 抢占行为。完整日志、输入指纹和限制见[启动验证记录](../validation/guest-docker-start.md)；
之前的 OOM 阻塞保留在[历史基础验收](../validation/guest-docker.md)。
