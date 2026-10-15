# Docker 启动链路：OOM、管道和设备过滤

基线为 `7d96b93f7158c8d9a3b3f321e5fda29dc80fbcf8`，日期 2026-10-15。
基线的 Linux 和 iPad CI 已通过；本次为后续开发代码，不升级 App 版本。

## 修复范围

- proc OOM 偏好：真实任务生命周期、线程共享、fork/exec、权限、范围、文件偏移。
  不实现 OOM killer，不修改宿主 OOM 策略。
- 匿名管道：两个端点共享 inode 权限与所有者；修复原版 shim 的 stdout fchown。
- cgroup device BPF：有限指令验证/解释、程序 FD 与组挂载生命周期、查询/替换，
  open/mknod/PTY peer 的实际拒绝；不向宿主加载 BPF。
- crun 空 blockIO 对象所需的 io.max 空视图，实际限速写入明确拒绝。
- Docker `/dev` 所需的 strictatime 和按需分配的 64 MiB tmpfs 配额。
- 空 mqueue 文件系统挂载、共享身份和 VFS 创建限制；mq_* 系统调用仍缺。
- `/dev` 枚举只在对应 bridge 设备存在时列出，修复特权设备扫描的 ENOENT。

## 验证

342 项 all-feature Rust 工作区测试通过；fmt、严格 all-target/all-feature Clippy、
ARM64 iOS core/distro/FFI 编译通过。新增的 C 夹具在 1/4 个 guest CPU 上验证
BPF 规则生效、祖先约束、FD 生命周期、替换、fork/迁移、EFAULT 和权限失败。
没有声称用宿主特权 BPF 做等价性比对。

Unix socket、ET、xattr、FIFO、匿名 pipe 元数据、PDEATHSIG、OOM 属性使用独立
原生 Linux 对照。OOM 对照仅修改测试进程自己的 proc 属性，先放弃有效
CAP_SYS_RESOURCE，以验证非特权最低值规则。

既有 namespace、cgroup/freezer/pids、原版签名 crun 的 1/4 CPU 生命周期、
多个 Alpine rootfs 与前台 PTY 回归通过。Docker 生成的特权 OCI 配置直接交给
原版 crun，已执行 id/hostname/shell；这一步仅是定位后续挂载问题的诊断，
不能代替 Docker 全链路验收。

1 CPU 和 4 CPU 全链路均已通过。普通 profile 使用默认设备过滤，
未开启 --privileged；payload 验证自身为 PID 1、输出标记、退出 7，Docker inspect
记录停止/退出 7，无 error；start 输出与 docker logs 均含同一标记，容器和镜像
删除成功。完整配置、包校验和及逐项结果见 [Docker 日志 JSON](guest-docker-start-results.json)，
源码和二进制指纹见 [数据](guest-docker-start-data.json)，其他回归输出见
[验证日志](guest-docker-start.log)。

## 范围和后续

BPF 为 CGROUP_DEVICE 子集：无 maps/helpers/stack/loops、网络 BPF、JIT 或
ALLOW_OVERRIDE。最多 4096 条指令/程序、128 个存活程序、16 个挂载/组。
规则只在现有 syscall 边界执行，无新增 CPU/native-memory 热路径锁。

POSIX 消息队列、io 限速、独立 IPC/net/user namespace、CPU/memory 控制器、
overlayfs、seccomp、rootless 与完整 Docker/OCI 仍不在本次验证范围。
OOM 非线程 CLONE_VM 进程间的 shared-mm 传播特例尚未实现。
本次没有 iPad 真机运行证据，也没有性能改善结论。
集成 runner 构建后只调整了空 mqueue 只读挂载的错误优先级（EROFS 优先于
普通创建 EPERM）及测试断言；最终代码重新通过 342 项工作区测试并重建。
该局部变更不涉及 Docker 测试采用的读写 mqueue 挂载。集成二进制与最终二进制
分别记录 SHA-256，避免混淆测试输入。

行为参考为公开的 Linux proc ABI、containerd 2.1.5 shim 和
[crun 1.23.1 eBPF 生成器](https://github.com/containers/crun/blob/1.23.1/src/libcrun/ebpf.c)。
未修改原版 Docker/containerd/crun 二进制；新增 Rust/C 代码采用 GPL-3.0-or-later。
