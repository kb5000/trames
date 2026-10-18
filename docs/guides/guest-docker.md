# 在 guest 内运行 Docker 容器

`feat/guest-docker-workflows` 接通原版 dockerd → containerd → runc、默认
IPC/cgroup/net namespace、seccomp/device 过滤和 bridge 网络。已发布的
**正式 1.0.0 build 26 IPA 不包含本批改动**；[1.0.0 build 27 测试 IPA](../validation/ipad-1.0-build27.md) 已包含网络和 overlay2。这不是宿主 Docker socket 接入。

网络支持默认 firewall/NAT、容器 DNS/TCP/UDP 出站、原版 Docker 内置 DNS、
自定义 bridge、同桥通信/跨桥隔离、DOCKER-USER、internal 网络和默认 TCP/UDP
端口代理。具体证据与平台边界见 [网络验证](../validation/docker-network.md)
和[运行时设计](../architecture/docker-network-runtime.md)。

## 安装与启动

当前开发分支使用 **Docker 默认的 runc + 自动管理 containerd**。安装原版签名包后直接启动：

```sh
apk add docker-engine docker-cli
dockerd >/tmp/dockerd.log 2>&1 &
```

`docker-engine` 会安装所需的 containerd/runc；不需要额外安装 crun、先启动
containerd、复制专用 daemon.json/containerd.toml，或设置 `GOMAXPROCS=1`。
默认探测选中 overlay2，并保留默认 bridge、iptables/ip6tables、转发/NAT 和
userland proxy。`GOMAXPROCS` 由 Go 和 guest 的 CPU 视图自行决定。
等日志出现 `API listen on /var/run/docker.sock` 后使用 Docker CLI。

测试环境为 Alpine 3.22.6 aarch64、Docker 28.3.3-r5、containerd 2.1.5-r2、
runc 1.3.4-r1。启动较慢时查看 `/tmp/dockerd.log`，不要重复启动多个 daemon。
iPad build29已包含默认runc及宿主MMU资源修复；build30起还自动准备启动tmpfs。

如果此前复制过本项目旧配置，移除其中 `containerd`、`default-runtime: crun`
与 crun runtime 注册项，或在备份自己的设置后改为默认空配置 `{}`。不要覆盖
其他有意设置的选项。此前手动启动的 containerd 也不再需要。
[daemon.json](../../examples/docker/daemon.json) 现在仅示意空的默认配置；
[containerd.toml](../../examples/docker/containerd.toml) 是可选的独立 daemon 示例，
Docker 管理模式无需安装它。

iPad build30起在Wayland、终端和服务启动前自动挂载 `/run`（0755）和
`/dev/shm`（1777，64MiB按需额度），直接运行dockerd即可。多个本地终端共享
这些挂载，冷启动得到新的临时内容。

独立CLI或其他未运行init的最小shell环境仍需手动准备；已挂载时跳过：

```sh
mkdir -p /run /dev/shm
mountpoint -q /run || mount -t tmpfs -o mode=755 tmpfs /run
mountpoint -q /dev/shm || mount -t tmpfs -o size=64m,mode=1777 tmpfs /dev/shm
```

这些挂载应在启动 dockerd 前完成；不要在已有活跃服务的 `/run` 上重复挂载。
`/run` 的 PID/socket 文件不能作为跨 guest 内核会话的活进程恢复。

overlay2 共享只读镜像层，首次修改文件才复制到上层。旧的 `vfs` 数据不自动
迁移；切换驱动后需重新导入或拉取镜像，旧驱动的数据不会自动删除。
[overlay 设计与边界](../architecture/overlayfs.md)及[存储验收](../validation/overlayfs.md)
保持各自范围；此前完整网络验收使用 vfs，不能替代 overlay2 的存储证据。

托管 resolver 为 `169.254.0.53`，由宿主系统 DNS 提供解析；它不是网关。
用户自定义 resolv.conf 保留。自定义 Docker 网络内的 `127.0.0.11` 由原版
Docker resolver 提供容器名称/alias 与外部 DNS。使用 full 网络模式。

## 运行与网络

导入官方 Alpine minirootfs 的解压 tar，避免将 Registry pull 与网络验收混淆：

```sh
docker import /tmp/alpine-rootfs.tar trames-alpine:probe
docker run --rm trames-alpine:probe \
  /bin/sh -ec 'ip a; ip route; nslookup dl-cdn.alpinelinux.org'
docker network create appnet
docker run --network appnet --name app \
  -p 8080:8080 trames-alpine:probe /path/to/server
```

最后一个命令需换成镜像内真实存在的服务程序；Alpine minirootfs 的 BusyBox
没有 httpd。UDP 使用 `-p 5353:5353/udp`。宿主浏览器访问 iPad 自身地址的
已发布端口；仍受 iPadOS 网络权限和应用前后台限制。`--network none`、
`--internal` 可限制外网，不能通过 unshare 绕过宿主网络模式。

不要求额外传入 `--ulimit nofile=256:256`；实际资源上限仍由 guest 内核提供。namespace/seccomp/device
是已实现的隔离范围；并不包含所有 SysV/POSIX IPC、user/rootless、完整 cgroup
CPU/memory/io controller。Registry pull、完整 volume/交互终端、
build/BuildKit、Compose 工作流仍单独跟进。AF_PACKET/ARP、STP/qdisc/tc、
所有 netfilter 扩展及 `--userland-proxy=false` 的宿主入口尚未覆盖。

## 验证

默认配置工作流的状态与边界见[默认运行时验证](../validation/docker-defaults.md)。

```sh
python3 tests/integration/docker-default-lifecycle.py exec
python3 tests/integration/docker-default-lifecycle.py storage
# 额外组合 bind/端口链路，解释执行较慢；状态以验证文档为准。
python3 tests/integration/docker-default.py
python3 tests/integration/docker-overlay.py
bash scripts/build-nftables-fixture.sh
bash scripts/build-netfilter-nat-fixture.sh
python3 tests/integration/nftables.py
python3 tests/integration/netfilter-nat.py
python3 tests/integration/docker-network.py --cpus 4
```

新磁盘安装签名 APK，只启动受控宿主 HTTP/UDP peer，不修改宿主网络。
iptables/NAT fixture 跑 1/4 vCPU，完整原版 Docker 网络单跑 4 vCPU；未变的
CPU/UI/GPU/guest libraries 不重复验收。历史隔离/内部 bridge 的证据保持其
原有范围，不能替代本次包含默认 firewall 的验证。
