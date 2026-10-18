# 在 guest 内运行 Docker 容器

1.0.0 后的开发分支已加入独立 network/IPC/cgroup namespace 与经典 BPF seccomp，
可测试 `docker create --network none --ulimit nofile=256:256 ...`，保留默认
IPC/cgroup/seccomp 配置。[隔离验证](../validation/container-isolation.md)和
[完整网络路线](../architecture/container-network-design.md)列出边界。
已发布的 1.0.0 IPA 不包含这批开发改动；bridge/NAT 尚未完成，下方的原有
daemon 配置仍禁用 bridge/iptables，不能直接删除这些限制配置来启用完整网络。
内部 veth/bridge、根 guest 与容器的 TCP/UDP 和 IPv4/IPv6 L3 转发已有独立
guest 验收。后续已接通原版 Docker 的内部 bridge 创建／容器运行路径，
但外部转发、nftables/NAT 和含防火墙的完整默认 bridge 尚未完成，
见[内部 bridge 范围](../validation/docker-bridge-internal.md)。

当前开发版可通过原版 Alpine Docker → containerd → crun 执行 Alpine 容器：
镜像导入、create/start、PID 1 payload、退出码、docker logs 与清理均纳入验收。
当前开发分支使用 vfs、network=none，保留默认独立 IPC/cgroup namespace、
seccomp 和设备过滤。早期共享 namespace 的配置见
[0.16.0/build 17 历史记录](../validation/release-0.16.md)。

## 配置

在 guest 内安装 `docker-engine docker-cli containerd crun`。测试使用 Alpine
3.22.6 aarch64、Docker 28.3.3-r5、containerd 2.1.5-r2、crun 1.23.1-r0，APK 仍由
原版 apk 验签。不要使用宿主 Docker socket。

将仓库的 [daemon.json](../../examples/docker/daemon.json) 和
[containerd.toml](../../examples/docker/containerd.toml) 分别放进 guest 的
`/etc/docker/daemon.json`、`/etc/containerd/config.toml`。
使用 `vfs` 存储，关闭 bridge/iptables；独立 netns 已支持，外部容器网络和
overlayfs 尚缺。vfs 会复制镜像层，文件操作和空间开销较大，目前不宣称 Docker 性能优化。

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
docker create --ulimit nofile=256:256 --name smoke --network none \
  trames-alpine:probe /bin/sh -ec 'echo hello'
docker start -a smoke
docker inspect smoke --format '{{.State.ExitCode}}'
docker logs smoke
docker rm smoke
docker image rm trames-alpine:probe
```

`--network none` 创建只有私有 lo 的独立网络，不能访问外网。此配置保留默认
IPC/cgroup/seccomp，验证其 namespace 身份和实际 seccomp mode 2；仍不代表
SysV IPC/mq_*、所有 cgroup 控制器或完整 Docker 隔离能力。

`--ulimit nofile=256:256` 与当前 guest FD 表上限一致。shim 所需的
`oom_score_adj`、pipe fchown/fchmod 和 cgroup device BPF 已实现；设备规则真正
限制新 open/mknod，容器内既有 FD 保持原有访问能力。

冷启动仍较慢，尚未做性能优化。Registry pull、build/buildkit、Compose、rootless、
Docker 容器 HTTP 服务和交互终端尚未验收。CDI/cgroup 监控的 inotify、pidfd、
CPU/memory/io 控制器等仍可能产生提示。`io.max` 仅允许空配置，限速写入失败；
`/dev/mqueue` 只有可挂载的空视图，POSIX 消息队列系统调用尚未实现。

## 自动验证

```sh
python3 tests/integration/docker.py --isolated --require-run
```

测试使用新磁盘和签名 APK，分别验证 1/4 CPU 的 daemon、image、create/start、
PID 1 payload、退出码 7、日志、remove。CI 使用 `--require-run`，启动失败不能
通过检查；省略该参数只用于单独记录阻塞的诊断。测试配置未使用 GODEBUG 改写
Go 抢占行为。完整日志、输入指纹和限制见[启动验证记录](../validation/guest-docker-start.md)；
之前的 OOM 阻塞保留在[历史基础验收](../validation/guest-docker.md)。

## 开发分支的内部 bridge

1.0.0 后开发分支可用原版 Docker 自动配置内部 IPv4 bridge。停止已有
dockerd 后，以下参数替代上面的 daemon 配置；containerd 保持同一配置：

```sh
dockerd --containerd=/run/containerd/containerd.sock --storage-driver=vfs \
  --iptables=false --ip6tables=false --bridge=docker0 --ip-forward=true --ip-masq=false \
  --add-runtime crun=/usr/bin/crun --default-runtime=crun >/tmp/dockerd.log 2>&1 &
```

创建容器时使用 `--network bridge`，保留默认 IPC/cgroup/seccomp/device。
根 guest 可以按实际容器地址访问其监听端口；不提供 SNAT、外部 DNS/HTTP
或发布端口。上述配置关闭了 Docker 防火墙与跨网络过滤，路由可达的 bridge
之间可转发，不能作为完整 Docker 网络隔离配置。
AF_PACKET／原始 ARP 尚缺，原版运行时的 unsolicited ARP 提示仍可能出现。

单独验收该路径，无需重跑原有 network=none 或全部图形／CPU 测试：

```sh
python3 tests/integration/docker.py --internal-bridge --cpus 1
```

此测试用新磁盘和原版签名包，检查默认隔离、容器 ping bridge 网关、根 guest
读取容器内 BusyBox nc 提供的 HTTP、退出码与清理；不验收防火墙或 NAT。

## 开发分支的 nftables 过滤

已接入 `NETLINK_NETFILTER` 的有界规则子集，原版 `iptables-nft`／`ip6tables`
可以在独立 guest 网络命名空间配置 filter 链，并实际丢弃匹配的内部 IP 包。
这一步没有接通 Docker 完整防火墙：NAT／conntrack／sets／xt 扩展以及根 guest
的原生 socket INPUT／OUTPUT 仍明确拒绝，以上 daemon 配置仍需保留。

单独验证这次改变：

```sh
bash scripts/build-nftables-fixture.sh
python3 tests/integration/nftables.py
```

测试只操作 guest 防火墙，用新磁盘和 Alpine 签名原版工具验证 1/4 CPU 的
IPv4／IPv6 过滤、规则生命周期、计数器和 namespace 策略。范围与限制见
[nftables 设计](../architecture/nftables.md)，不需要重跑已通过的 Docker 镜像
导入或其他未变功能。
