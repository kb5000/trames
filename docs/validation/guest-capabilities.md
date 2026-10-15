# Guest capability 与原版容器工具探测

日期：2026-10-15。基线：`df16b0721620eb50d86b5374165da51738d57dc6`。
本批新增 capget/capset，扩展 prctl 和实际权限判断；不更改应用版本号。

## 结果

- 1/4 CPU guest 测试通过：v1/v2/v3 ABI、版本协商、跨页 EFAULT 不提交状态、
  permitted/effective/inheritable 限制、跨进程只读查询、PID namespace 的本地 TID。
- 能力丢弃实际约束 root 的挂载、UTS/PID namespace、chroot、UID/GID/groups、
  原始 socket、DAC 文件读取/写入、chmod/chown、跨 UID 信号及硬资源上限。
- 验证 UID/effective UID/fsuid 规则、KEEP_CAPS、securebits 锁与 NO_SETUID_FIXUP、
  bounding drop、inheritable 例外、ambient 跨 exec 与清除、NOROOT、NNP 的
  不可撤销性和 setuid/普通 exec 行为。exec 重置 fsuid；SGID 无 group execute
  位时不切换有效 GID。单线程 capset 不污染其他线程，fork 修改不污染父任务。
- 无特权 ABI 子集同时编译为宿主程序，对照实际 Linux 的版本协商、空能力集合、
  拒绝恢复权限、ambient 清除、NNP 参数和不可撤销行为。宿主没有 root 能力，
  因此不把 guest 特权测试声称为宿主特权差分。
- 原版签名 Alpine `libcap-utils=2.78-r0`：capsh --print、指定能力查询、bounding
  drop、keepcaps/setuid、ambient exec、no_new_privs、实际 mount/chroot 拒绝均通过。
  工具使用 `--shell=/bin/sh`，因为 Alpine 最小 rootfs 没有默认的 `/bin/bash`。
- 原版 `sudo=1.9.17_p2-r0`：root/run-as/普通用户提权、真实密码认证、拒绝未授权
  用户、交互输入输出、Ctrl-C、嵌套 PTY 和持久化重开通过。
- 既有 1/4 CPU UTS/mount/PID 测试和原版 BusyBox 工具通过；每种 CPU 配置的
  23 项 fd-exec 原生 Linux 对照、runtime-compat、guest-pty 回归通过。
- 315 个 workspace all-feature Rust 测试、strict Clippy、fmt 和 ARM64 iOS FFI
  编译检查通过。这里没有签名/链接新的 IPA，也没有进行 iPad 真机验收。
- 系统调用清单：176 个 implemented subset、140 个 missing、2 个 obsolete；
  142 个未分派编号逐一探测均返回 ENOSYS。CPU 指令覆盖未改。

## crun 实际停止位置

原版签名 `crun=1.23.1-r0`（上游 commit
`d20b23dba05e822b93b82f2f34fd5dada433e0c2`）可执行 `--version` 和 `list`。
探测使用有效 OCI 1.0.2 bundle、静态 BusyBox `/bin/true`、mount/PID/UTS namespace、
NNP、选定能力集合，显式设置 `--cgroup-manager disabled`，仍停止于：

```text
Loading container from config file: `config.json`
Writing config file to: `/run/crun/probe/config.json`
statfs `/sys/fs/cgroup`: No such file or directory
```

run 的退出状态为 1，**没有启动容器**。dmesg 中的 open_tree ENOSYS 为功能探测，
不是这次退出的最终原因。下一步以这条真实路径继续补 cgroup/进程生命周期，
随后定位 Unix IPC、user namespace 等实际需要的接口；不添加假成功端点。

可复现诊断脚本会保存每步退出状态、原始输出、bundle 和 APK SHA-256，
将阻塞标成 BLOCKED。它是诊断工具，不是容器通过测试，未放入 CI 充当通过项。
capability 测试和原版 capsh 已加入 Linux CI。

```sh
source /tmp/trames-vulkan-env.sh # 仅本次云环境的工具链设置，其他环境按现有构建文档准备
cargo build --release -p trames-cli
bash scripts/build-capability-fixtures.sh
python3 tests/integration/capabilities.py
python3 tests/integration/container-probe.py
python3 scripts/audit-syscalls.py
```

[原始日志](guest-capabilities.log) · [源码/验证指纹](guest-capabilities-data.json) ·
[原版 crun 结果与包指纹](guest-capabilities-crun.json) ·
[实现范围与后续缺口](../architecture/guest-containers.md)

当前只实现初始 guest user namespace 的任务能力。文件 capability xattr、
user namespace 的 uid/gid map 和权限层级、完整 dumpable/ptrace、seccomp、
cgroup 以及 IPC/net namespace 仍缺。能力位存在不代表对应内核设施已经实现。
权限元数据只在现有系统调用/调度边界访问，没有加入 CPU/native-memory 热路径锁；
本批没有性能跑分或新的加速比主张。
