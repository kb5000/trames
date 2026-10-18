# Docker 默认运行时验证

范围：Alpine 3.22.6 aarch64，原版签名 Docker 28.3.3-r5、containerd 2.1.5-r2、
runc 1.3.4-r1。默认启动 dockerd，自行管理 containerd，使用 runc/overlay2；
不安装 crun，不传 GOMAXPROCS、nofile、runtime、storage、安全或网络覆盖配置。

实现见 [默认启动设计](../architecture/docker-defaults.md)，生命周期复现入口为
`tests/integration/docker-default-lifecycle.py exec|storage`，额外组合链路为
`tests/integration/docker-default.py`。正常启动所需的 /run、/dev/shm tmpfs
在服务启动前挂载。宿主仅提供受控 HTTP peer 和端口客户端，不调用宿主 Docker。

## 已完成的局部验证

- Linux net 40 个单元和 11 个虚拟网络测试通过。
- 首轮 Linux VFS 144 个通过、1 个忽略；Apple 为 143 个通过、1 个忽略。
- Linux socket ABI 3 项、调度器 timeout 1 项、seccomp 4 项通过。
- 受影响模块 strict clippy、格式和脚本语法检查通过。
- 独立 C 夹具：宿主 Linux UDP 对照、guest 1/4 CPU netlink 和 4 CPU UDP；
  阻塞超时、MSG_DONTWAIT、dup/fork 共享、SA_RESTART 信号打断、错误参数和
  readlinkat 空路径通过。宿主环境不允许创建 netlink socket，故宿主对照用 UDP。
- 独立 seccomp C 夹具：宿主 Linux、guest 1/4 CPU 均验证 prctl 安装、拒绝
  指定 syscall、fork 后继承。过滤器真正执行。

源码 a7306222 的 [Apple/iOS](https://github.com/kb5000/trames/actions/runs/39114128260)
针对性检查已通过。Linux 同批模块检查和 C ABI 对照通过；默认配置的本地
容器运行、PID1/eth0/cgroup、退出和清理通过（4 CPU，475.630 秒）。

首次完整 CI 在 runc 只读 bind mount 重设标志时失败：MS_REC 与 bind remount
组合被误拒绝。已按 [Linux v6.12 path_mount](https://github.com/torvalds/linux/blob/v6.12/fs/namespace.c)
修正；14 项挂载回归和 clippy 本地通过，包含源/子挂载属性不变的检查。
首轮 artifact #12131568019（14000 bytes）核验 SHA256
`36ae367b6945f4dd988312d1db03eaab829da13b8036194f64ca3061b2800d60`。
后续只重跑挂载回归、iOS FFI 和未完成的默认 Docker 工作流；未变的模块和
C ABI 对照按精确源码补丁 hash 与 CI 结果复用。

修正后组合验收在 `docker exec` 阶段触发 1800 秒总超时。此前已通过默认
daemon/containerd/runc、只读 bind mount、实际设备策略拒绝、Seccomp=2、
PID/cgroup、169.254.0.53 DNS 和受控 HTTP 出站。artifact #12132698705
（1793 bytes）的 SHA256 为
`ed9b4bfd1c53005b84b832a39707207e7ba1bf538ceeaa2d066b51623813cfa2`。
未保存 host browser 的完成结果，不能据此宣称该轮宿主 TCP 访问或 exec 成功。

后续将 exec 与存储/重启拆为两个独立 guest，并行验收未完成部分，避免每次
组合验收都重复等待 bind/端口链路。新入口为
`tests/integration/docker-default-lifecycle.py exec|storage`；运行时源码仍为
505e1c4，模块/C ABI/Apple 检查均复用。

存储生命周期已通过 [Linux 独立验收](https://github.com/kb5000/trames/actions/runs/39118090096)
的 job #117647914965：默认 runc/overlay2、容器停止、upper 文件持久化、未改动
BusyBox 不复制到 upper、daemon 重启恢复、容器/镜像清理。4 guest CPU、
1172.181 秒。artifact #12134115545（7773 bytes）SHA256 核验为
`f402037288869a44fcf0d142170b481643527bbe0b1db0651739697849021164`。
exec 独立验收也已通过同一 run 的 job #117647914939：原版 `docker exec` 执行、
结果读取、文件和 proc inode 检查、容器正常退出和清理。4 guest CPU、1854.747 秒。
artifact #12133901582（8100 bytes）SHA256 核验为
`080edfe15a24a476bf32da1fc98e6939b711eec727508c5be1609f770453a790`。
两条验收的 runner、shell、rootfs 和签名包输入 hash 一致。原始指纹见
[JSON 记录](docker-defaults.json)。运行时源码为 505e1c4，测试拆分源码为 56a8a3a。

此处为 Linux x86_64 CI 主机解释执行 ARM64 原版包；时长不是 iPad 实测。
runc 容器创建仍较慢，后续需要单独采样定位；本次没有用 Go/nofile 参数规避。

## 工作流覆盖与边界

本批确认默认 daemon/containerd/runc、overlay2、seccomp 状态、设备策略实际拒绝、
PID/cgroup 视图、容器 DNS/受控 HTTP 出站、docker exec、upper 文件持久化/
未变文件不复制、daemon 重启和清理。组合链路中的独立宿主 TCP 回调没有保存
完成结果；本轮不将它记为通过，历史宿主端口访问证据保持原有范围。

历史完整双栈/UDP/bridge/firewall 网络和 overlay commit/diff 结果保持原有范围，
本次不重复未改动的全部工作流。不将插件加载解释成完整 CRI/Kubernetes/NRI 验收。
Unix socket timeout、user namespace/rootless、全部 cgroup controller、Registry、
交互终端、BuildKit/Compose 等仍是独立工作。当前测试 IPA build 27 不含本次修正；
iPad 实机的默认 runc 路径仍待验证。
