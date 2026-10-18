# 在 guest 内运行 Docker 容器

`feat/guest-docker-workflows` 接通原版 dockerd → containerd → crun、默认
IPC/cgroup/net namespace、seccomp/device 过滤和 bridge 网络。已发布的
**正式 1.0.0 build 26 IPA 不包含本批改动**；[1.0.0 build 27 测试 IPA](../validation/ipad-1.0-build27.md) 已包含网络和 overlay2。这不是宿主 Docker socket 接入。

网络支持默认 firewall/NAT、容器 DNS/TCP/UDP 出站、原版 Docker 内置 DNS、
自定义 bridge、同桥通信/跨桥隔离、DOCKER-USER、internal 网络和默认 TCP/UDP
端口代理。具体证据与平台边界见 [网络验证](../validation/docker-network.md)
和[运行时设计](../architecture/docker-network-runtime.md)。

## 安装与启动

在 guest 中安装签名原版包：

```sh
apk add docker-engine docker-cli containerd crun
```

测试版本为 Alpine 3.22.6 aarch64、Docker 28.3.3-r5、containerd 2.1.5-r2、
crun 1.23.1-r0。把 [daemon.json](../../examples/docker/daemon.json) 与
[containerd.toml](../../examples/docker/containerd.toml) 分别放入
`/etc/docker/daemon.json`、`/etc/containerd/config.toml`。采用 overlay2 写时复制存储和
正常的 iptables/ip6tables/docker0/ip-forward/ip-masq，保留默认
**userland-proxy=true**。不要使用旧阶段关闭 firewall/NAT 的开发配置。

新 guest 会话需 `/run` 和 `/dev/shm` tmpfs；若初始化系统已挂载则不重复挂载。
`/run` 的 PID/socket 文件不能当作跨 guest 内核会话的活进程恢复。

```sh
mkdir -p /run /dev/shm
mount -t tmpfs -o mode=755 tmpfs /run
mount -t tmpfs -o size=16m,mode=1777 tmpfs /dev/shm
export GOMAXPROCS=1
containerd --config /etc/containerd/config.toml >/tmp/containerd.log 2>&1 &
```

等 `containerd successfully booted` 后启动：

```sh
dockerd --config-file /etc/docker/daemon.json >/tmp/dockerd.log 2>&1 &
```

等 `API listen on /var/run/docker.sock` 后使用 CLI。先单独启动 containerd，
避免 dockerd 内置启动期限；解释执行下冷启动仍慢。`GOMAXPROCS=1` 是验收
设置，不改变 Trames vCPU 数。overlay2 共享只读镜像层，首次修改文件才复制到上层。
旧的 `vfs` 数据不自动迁移；切换驱动后需重新导入或拉取镜像，旧驱动的数据不会自动删除。
[overlay 设计与边界](../architecture/overlayfs.md)及[存储验收](../validation/overlayfs.md)
单独记录；此前完整网络验收使用 vfs，不重复作为 overlay2 的存储证据。

托管 resolver 为 `169.254.0.53`，由宿主系统 DNS 提供解析；它不是网关。
用户自定义 resolv.conf 保留。自定义 Docker 网络内的 `127.0.0.11` 由原版
Docker resolver 提供容器名称/alias 与外部 DNS。使用 full 网络模式。

## 运行与网络

导入官方 Alpine minirootfs 的解压 tar，避免将 Registry pull 与网络验收混淆：

```sh
docker import /tmp/alpine-rootfs.tar trames-alpine:probe
docker run --rm --ulimit nofile=256:256 trames-alpine:probe \
  /bin/sh -ec 'ip a; ip route; nslookup dl-cdn.alpinelinux.org'
docker network create appnet
docker run --ulimit nofile=256:256 --network appnet --name app \
  -p 8080:8080 trames-alpine:probe /path/to/server
```

最后一个命令需换成镜像内真实存在的服务程序；Alpine minirootfs 的 BusyBox
没有 httpd。UDP 使用 `-p 5353:5353/udp`。宿主浏览器访问 iPad 自身地址的
已发布端口；仍受 iPadOS 网络权限和应用前后台限制。`--network none`、
`--internal` 可限制外网，不能通过 unshare 绕过宿主网络模式。

`--ulimit nofile=256:256` 对应当前 guest FD 上限。namespace/seccomp/device
是已实现的隔离范围；并不包含所有 SysV/POSIX IPC、user/rootless、完整 cgroup
CPU/memory/io controller。Registry pull、volume、exec/交互终端、
build/BuildKit、Compose 工作流仍单独跟进。AF_PACKET/ARP、STP/qdisc/tc、
所有 netfilter 扩展及 `--userland-proxy=false` 的宿主入口尚未覆盖。

## 验证

```sh
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
