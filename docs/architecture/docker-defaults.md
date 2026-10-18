# 标准 Docker 启动路径

默认使用原版 `dockerd → containerd → containerd-shim-runc-v2 → runc`，全部在
同一个 guest 内执行。Trames 提供 Linux ABI、虚拟进程/文件系统和网络；不替换
这些上游二进制，也不接入宿主 Docker。

## 配置

`apk add docker-engine docker-cli` 后直接运行 `dockerd`。由 dockerd 发现并
启动自己的 containerd，自动探测 overlay2、使用默认 runc 和正常的安全/网络策略。
不额外指定外部 containerd、crun、`GOMAXPROCS=1` 或容器 nofile 值。

旧示例中的 CRI images/runtime 与 NRI 禁用列表已移除；Docker 自行生成的
containerd 配置保留上游行为。插件能够加载不等于完整 Kubernetes CRI、NRI
资源管理、containerd snapshotter 或资源控制器已经验收。

所测 Docker 28.3.3 仍会自行生成
`disabled_plugins = ["io.containerd.grpc.v1.cri"]`，这是上游配置，保留原样。
删除的是本项目额外手写的 CRI images/runtime 和 NRI 禁用项；不修改
`/run/docker/containerd/containerd.toml`。containerd 2.1 的 images/runtime、
NRI 在该配置下可以加载。

最小 rootfs 中的 `/run`、`/dev/shm` tmpfs 是常规启动挂载，不属于 Docker 的
配置文件。若运行 init，由 init 完成；手动挂载必须在启动服务前进行。
guest 总资源上限仍然有效。移除 nofile 参数不等于取消 FD 上限。

## runc 所需的 procfs 语义

- proc 根 inode 使用 Linux `PROC_ROOT_INO=1`。原版 runc/pathrs 会同时检查
  inode 和 procfs superblock 类型；保留这些检查。
- 目录元数据定位比较 `(device, inode)`，避免 proc 根 inode 与普通文件系统
  根 inode 同为 1 时被误认为同一节点。
- `readlinkat(fd, "")` 支持 Linux 隐式 AT_EMPTY_PATH：O_PATH/O_NOFOLLOW
  symlink FD 直接引用 inode，普通 symlink 在 unlink 后仍可读取；proc FD magic
  link 则刷新当前目标和权限，目标 FD 关闭后返回 ENOENT。
- coordinator 将 proc symlink FD 的 readlinkat 计入 proc 快照刷新，避免安全
  reopen mountpoint 时看到过期的 FD 表。
- sysctl 的 O_PATH 句柄重开保留原 network namespace，O_TRUNC 不截断合成的
  proc inode；实际写入仍执行权限和挂载只读检查。
- `/proc/PID/setgroups` 与 task 别名报告初始 user namespace 的 `allow` 策略。
  当前尚不提供 user namespace 或可写的 setgroups 策略。
- bind remount 接受 runc 传入的 MS_REC；与 Linux `path_mount()` 一致，
  MS_REMOUNT|MS_BIND 只修改当前挂载，源挂载和子挂载各自保留属性。
  这不等同于 mount_setattr 的递归属性修改。

## seccomp

`prctl(PR_SET_SECCOMP, mode, filter)` 的模式 1/2 映射到 `seccomp(2)` 的
操作 0/1，共用现有 strict/filter 实现；不将 mode 当作 operation 直接分派。
原版 runc 安装的过滤器真实执行，不通过禁用 Docker 默认 seccomp 绕过。
独立 C 夹具在宿主 Linux 和 guest 1/4 CPU 下安装拒绝指定调用的 BPF，并
验证 fork 后的继承。容器验收另检查 cgroup 设备策略实际拒绝非允许设备。

## socket 收发超时

原版 runc 的 netlink 库使用 `SO_RCVTIMEO`/`SO_SNDTIMEO`。TCP、UDP、netlink
支持这两个选项及 LP64 的 NEW 编号；AArch64 使用两个 64 位字段的 timeval。
optlen/指针和微秒范围在状态变更前验证。零值恢复无限等待；负秒数为立即到期。

超时状态属于共享 socket，dup/fork 保留同一对象，accept 得到监听 socket 的
超时设置副本。宿主 FD 始终非阻塞，不调用宿主阻塞 setsockopt 来拖住 coordinator。
每个 guest task 的阻塞调用保存一次截止时间，重试不延期；CPU 并行 checkpoint
和 idle wait 都纳入截止时间。停止/运行中的 task 不因旧截止时间导致忙轮询。

recv/send/read/write/accept 到期返回 EAGAIN；connect 的发送超时返回 EINPROGRESS。
已有部分传输立即返回，不重放整个调用。O_NONBLOCK/MSG_DONTWAIT 不等待，poll
继续使用自己的超时。带 socket 超时的调用被处理的信号打断后返回 EINTR，
即使 handler 设置 SA_RESTART 也不自动重新等待。Unix socket 超时仍未覆盖。

CLI 新建 resolver 配置与 iPad 一致，使用 `169.254.0.53`；该地址可从容器
网络访问，避免 Docker 过滤 loopback nameserver 后改用外部公共 DNS。
已有用户 resolver 不覆盖。

独立生命周期验收入口为 `tests/integration/docker-default-lifecycle.py exec|storage`，
额外组合 bind/端口链路为 `tests/integration/docker-default.py`，使用原版签名包和正常的
Docker CLI；具体通过范围见[验证记录](../validation/docker-defaults.md)。
socket timeout C ABI 夹具提供宿主 Linux UDP 对照和 guest
1/4 CPU netlink/UDP、信号、错误优先级与 FD 共享验证。
