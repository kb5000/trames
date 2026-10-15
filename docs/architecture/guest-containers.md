# Guest 内容器：namespace 基础

目标是在同一个 Trames guest 内运行原版 Linux 容器工具。容器共享解释器、
调度器和 guest 内核对象；当前不做宿主多实例管理器或自己的 OCI 启动器。
后续用未经修改的 guest `runc` / `crun` 验证 Linux ABI，再考虑 Docker/Podman。

## 已实现

| 接口 | 当前范围 |
| --- | --- |
| `unshare` | `CLONE_NEWUTS`、`CLONE_NEWNS`、`CLONE_NEWPID`、`CLONE_FS`、`CLONE_FILES`；未知组合与额度失败不改变调用者状态；NEWNS 隐含独立 fs context |
| `clone` | UTS/mount/PID namespace；NEWNS 与 CLONE_FS、NEWPID 与 THREAD/PARENT 冲突返回 EINVAL；PID 1 不接受 CLONE_PARENT |
| `setns` | UTS/mount/PID namespace FD，类型 0 或对应 CLONE 标志；mount 拒绝共享 fs context；PID 仅接受当前或后代 namespace，影响后续子进程 |
| `uname`、`sethostname`、`setdomainname` | 调用者 UTS，名称最长 64 字节；按 guest CAP_SYS_ADMIN 授权 |
| namespace FD | `/proc/PID/ns/{uts,mnt,pid,pid_for_children}`、task/TID 形式、dup、O_PATH、stat、nsfs statfs、`NS_GET_NSTYPE`；FD 保持 namespace 存活 |
| `chroot` | 独立 `(mount,inode)` fs.root，绝对路径/绝对符号链接从新根解析，根内 `..` 不能向上；保留 cwd/dirfd |
| `mount` | tmpfs、proc、sysfs、共享 devpts、bind/rbind、选定 remount；所有挂载初始为 private，支持 make-private/recursive-private |
| `umount2` | 普通卸载检查子挂载和 FD/cwd/root/exec/mmap 引用；MNT_DETACH 断开整个子树，已有能力仍能访问；UMOUNT_NOFOLLOW |
| `pivot_root` | 挂载根和 put_old 目录/祖先关系检查，旧根接到新根下，同 namespace 内位于旧根的 fs.root/cwd 一并切换 |
| 挂载视图 | `/proc/PID/{mounts,mountinfo}` 来自实际挂载树，按该进程 chroot 过滤/显示路径并转义字段；statfs/statx MNT_ID/fdinfo 使用实际挂载身份 |

## PID 层级与生命周期

调度器保留稳定的全 guest 任务编号，只在系统调用、信号信息和 proc 边界转换。
每个子 namespace 独立分配 PID，祖先可以看见后代，反向与兄弟之间不可见。
`unshare(CLONE_NEWPID)` 和 PID `setns` 设置 `pid_for_children`，调用者本身不迁移；
进入新 namespace 的首个子进程成为 PID 1，并建立自己的进程组/会话。
多线程调用 NEWPID unshare、NEWPID 与 THREAD/PARENT 冲突组合明确拒绝。

`getpid/gettid/getppid`、clone 返回值与 TID 内存写入、wait4/waitid、kill/tkill/tgkill、
进程组/会话、affinity、进程查询、tty group/session、GETLK owner 和信号发送者均
使用对应的 namespace 视图。命名空间外的父进程或信号发送者显示为 0。
非 leader 线程 exec 保留线程组进程身份；exec 不把进程迁回根 namespace。

PID 1 未安装处理器时忽略普通终止信号；祖先发来的 SIGKILL/SIGSTOP 可强制作用。
最后一个 init 线程退出会在调度边界清理该 namespace 及所有后代的剩余任务，
保留外部父进程可 wait 的退出状态；保留的死 namespace FD 再 fork 返回 ENOMEM。
孤儿先交给同 PID namespace 内最近的 child subreaper，再交给该 namespace 的 init。
`prctl` 新增 PR_SET_CHILD_SUBREAPER / PR_GET_CHILD_SUBREAPER，保留已有
PR_SET_NAME / PR_GET_NAME；能力相关选项见下节，其他选项返回 EINVAL。

每次新建 proc 挂载绑定调用者**当前** PID namespace；bind、挂载 namespace 复制、
打开的 proc FD 保留原绑定。仅 unshare PID 不会改变既有 `/proc`。新挂载的 proc
过滤进程列表，并转换 self/thread-self、stat/status、task、fd/exe/ns 等端点；
status 的 NStgid/NSpid/NSpgid/NSsid 从该 proc 的 namespace 起显示层级。
缓存按 PID namespace 和路径区分，避免不同容器的 `/proc/1` 共享内容。

waitid 支持 P_ALL/P_PID/P_PGID、EXITED/STOPPED/CONTINUED、NOHANG/NOWAIT 和 WALL；
输出先检查，NOHANG 无事件清零 siginfo。P_PIDFD、非 SIGCHLD clone 子类和完整
rusage 统计尚缺（wait4/waitid 的 rusage 当前均为零）。无 PID 分配/映射操作进入
CPU 指令或 guest memory 热路径；嵌套 proc 查询会做进程快照过滤与编号转换。

## Capability 与执行权限

每个任务保存 effective/permitted/inheritable/bounding/ambient 五组 41 位掩码、
securebits 及不可撤销的 no_new_privs。fork/clone 复制状态，线程不共享可变凭据；
没有增加 CPU/访存热路径锁。capget v1/v2/v3 可查询可见的 namespace-local TID，
capset 只修改调用线程，执行 permitted 子集、effective 子集、inheritable 与
bounding/SETPCAP 规则；失败不提交状态。

prctl 支持 KEEPCAPS、SECUREBITS 及锁位、CAPBSET_READ/DROP、NO_NEW_PRIVS、
ambient IS_SET/RAISE/LOWER/CLEAR_ALL。UID/effective UID/fsuid 转换采用各自的能力
调整规则。exec 清除 KEEP_CAPS，保留锁位、bounding/inheritable/NNP；普通非 root
执行只得到 ambient 能力，传统 root 规则可从 bounding/inheritable 恢复权限，
NOROOT 禁止这一行为，NNP 阻止新增 permitted 和 set-ID 提权。MS_NOSUID 仍生效。
丢 bounding 不立即撤销现有 permitted，也不删除已在 inheritable 中的能力；
需要永久禁权的工具必须同时设置对应集合或 NOROOT/NNP。

实际检查已接入挂载/namespace/UTS 的 SYS_ADMIN、chroot 的 SYS_CHROOT、
SETUID/SETGID、文件 DAC_OVERRIDE/DAC_READ_SEARCH/CHOWN/FOWNER/FSETID、
KILL、SYS_NICE、SYS_RESOURCE、SYSLOG 和 NET_RAW。既有 proc 敏感路径的 root
例外改用 SYS_PTRACE，并加入下述 dumpable/凭据检查。UID 0 不再绕过这些
检查。`/proc/PID/status` 显示五组 Cap 字段及 NoNewPrivs，cap_last_cap 为 40。

当前只有初始 guest user namespace；位掩码可表达某项能力不代表该项 Linux
设施已实现。文件 security.capability xattr、UID/GID 映射、user namespace
权限层级、完整 ptrace、seccomp 仍缺。不冒充宿主 Linux 权限，也不更改 iPad 凭据。

原版签名 Alpine capsh 2.78 已可查询/丢弃/保留能力并测试 ambient/NNP 跨 exec。
crun 1.23.1 已通过 cgroup 查询、dumpable、设备创建与权限设置、根目录切换和
setsid，走到 `Writing container status`，随后因 signalfd4 ENOSYS 退出 1。
探测显式禁用 cgroup manager，采用 private 传播配置；尚未完成容器运行验收。
open_tree 和 keyctl 的 ENOSYS 走了运行时自身回退，不是这次终止原因。
下一步接入 signalfd 与现有信号队列、阻塞读取及 poll/epoll。

## 设备节点与容器路径

mknodat 可创建字符/块设备 inode，需要 CAP_MKNOD，并执行目录权限、umask、
只读挂载检查。null/zero/full/random/urandom/tty/ptmx/kmsg 按 Linux 设备号
复用 guest 驱动；未注册的设备号可以 stat、O_PATH 查询和持久化，数据打开返回
ENXIO。不打开宿主设备，也不提供块驱动或独立 devpts；PTY 仍使用会话内共享池。
设备支持硬链接、改名、chmod/chown、unlink 后的 FD 保活；getdents 区分字符与
块类型。合成的易失 `/dev` 端点不能硬链接进持久 inode 表，仍可用 mknodat 创建
相同设备号的节点。磁盘 v3 manifest 新增设备类型和 32 位设备号，不保存活跃终端状态。
已有常规文件和 FIFO 的 dev 参数被忽略；socket inode mknod 仍不支持。

openat2 支持 NO_XDEV、NO_MAGICLINKS、NO_SYMLINKS、BENEATH、IN_ROOT。
检查发生在 mount/inode 遍历中：BENEATH 拒绝越界和绝对链接，IN_ROOT 将绝对
路径、绝对链接和根部的 `..` 限定在 dirfd；NO_XDEV 同时识别 bind mount。
最终 O_PATH|O_NOFOLLOW 可以引用链接自身。CACHED 返回 EAGAIN，不宣称无 I/O
查找保证。路径元数据变更由现有 syscall 边界串行处理，没有增加访存热路径锁。

chmod/chown 通过 `/proc/PID/fd/N` 操作实际 inode 与挂载，不按显示路径重新打开；
包括取消链接后同名重建。相关元数据调用会刷新 proc 快照，避免 FD 复用时取到旧对象。
支持原版 crun 的 `pivot_root(".", ".")`，随后通过旧根 FD 执行 MNT_DETACH；
父 mount namespace 和显式保留的旧根 FD 保持各自语义。

新 PID namespace 的 PID 1 继承父 PGID/SID/控制终端，不提前建立新会话。
祖先组/会话编号在新 namespace 内显示为 0，首次 setsid 可建立编号 1 的会话。
这一行为按 Linux v6.12 `copy_process` 校正，普通会话首领再次 setsid 仍返回 EPERM。

## Cgroup v2 域成员与 dumpable

`/sys/fs/cgroup` 是独立的 cgroup2 挂载，具有自己的 inode、设备号和 statfs magic；
`/proc/filesystems`、`/proc/cgroups`、mountinfo 和 `/proc/PID[/task/TID]/cgroup`
与它一致。组目录、cgroup.procs/threads、type、events、stat 反映实际任务；
向 procs 写入 PID 或 0 迁移整个线程组，fork 继承，退出和非主线程 exec 清理旧成员。
成员读写使用调用者 PID namespace 的编号，可见性与 proc 挂载视图分别处理。

层级在单个 guest 内共享，mount namespace 分离不会复制它；重启 guest 后清空，
不占持久根磁盘额度。最多 128 个组（包括被打开对象保活的已删除组）、32 层嵌套。
删除后重建同一路径不会将旧 FD 或 bind mount 重定向到新组。只读重挂载实际限制
写入。状态仅在现有 syscall/调度边界修改，没有加入 CPU/guest memory 热路径锁。

这是域成员子集：controllers 为空，非空 subtree_control 请求显式拒绝；没有
CPU/memory/pids 限额、threaded 模式、freeze/kill、事件通知、cgroup namespace
或向非 root 用户委派。组管理和迁移要求 CAP_SYS_ADMIN；节点由 root 持有，
不开放 chmod/chown 委派。不能把 populated/frozen 文本或目录存在视为资源隔离。

PR_GET/SET_DUMPABLE 支持 0/1，CLONE_VM 共享，fork 复制，exec 建立新的状态；
有效/fs UID/GID 变化清零，普通 exec 恢复，能力增加、不可读 ELF 或 ID 不匹配
保持不可转储。敏感 proc 数据打开、magic link 访问采用选定 READ_FSCREDS 检查：
同线程组或 SYS_PTRACE 例外，否则检查 dumpable、UID/GID 和 permitted 能力子集。
普通 stat/lstat/O_PATH 元数据查询仍可用。没有实现 core 文件写出或完整 ptrace。

## 挂载身份与文件内容

路径解析、cwd、root、打开的文件、执行中的 ELF 和文件映射保存挂载引用。
同一个 inode 经不同 bind mount 可具有不同的挂载 ID 和权限。bind 不复制文件；
底层修改通过两条路径可见，跨挂载 rename/link 返回 EXDEV。
`..` 在挂载根跨回挂载点的父目录，绝对符号链接从进程根重新解析。

mount namespace 复制挂载对象，保留底层文件系统/inode；不会复制根磁盘内容。
既有 FD 保持原挂载能力，不因 unshare 被改写。惰性卸载保存仍有能力引用的
子树，最后引用释放后回收额度。proc 快照持有弱引用，避免历史视图保活。

`MS_RDONLY` 约束路径修改、FD 写入/截断和共享可写映射；`MS_NOSUID` 影响
exec 凭据；`MS_NODEV` 拒绝设备打开；`MS_NOEXEC` 拒绝 exec、可执行 mmap，
并保留映射的最大执行权限供后续 mprotect 检查。已有可写映射使只读重挂载
返回 EBUSY。这是选定模型，不声称实现 Linux superblock 的全部 remount 语义。
`MS_RELATIME` 在普通文件 read/pread 时按 mtime/ctime 或 24 小时条件更新 atime；
`MS_SILENT` 和旧版 mount magic 被兼容。不支持的挂载行为明确返回错误。

这些对象只在现有内核调度边界访问，没有给普通 CPU 指令/访存增加锁或权限检查。
挂载点使用缓存路径验证，祖先改名后才重新查找 inode，避免每次 lookup 扫描磁盘树。

## 配额与尚缺的部分

- 每个 guest 会话最多 128 个存活挂载对象，含 namespace 副本和被 FD/映射保留的
  挂载；耗尽 ENOSPC，释放后可再创建。UTS 和 PID 谱系各有 128 个存活对象额度。
  PID 最多嵌套 32 层，每会话仍最多 32 个任务；编号不循环复用，fork 前回收无引用映射。
- tmpfs 独立于磁盘 manifest，默认 16 MiB / 1024 inode；`size`、`nr_inodes` 可降低
  上限，`mode` 设置根目录权限。卸载/会话结束后内容不持久化。这里的内存上限
  与根磁盘默认 16 GiB 预算无关；尚未接入统一的 tmpfs/guest RAM 回收机制。
- 所有挂载均为 private；shared/slave/unbindable 传播、MS_MOVE、overlayfs、块设备
  文件系统及 open_tree/move_mount/fsopen 等新挂载 API 尚缺。
- proc/sysfs 保持当前只读端点；devpts 挂载共享现有 PTY 池，明确不接受
  `newinstance`，不能宣称容器已有独立终端或设备隔离。
- 跨进程 `/proc/PID/root`、cwd 等完整 magic-link 能力、user namespace、
  IPC/net namespace、cgroup 资源控制与委派、seccomp
  仍缺。现在不能运行一般意义上的完整容器，也不构成完整隔离边界。

`chroot` 不自动 chdir/关闭既有目录描述符；根外 cwd/dirfd 继续是可用能力。
程序应自行 `chdir("/")`。本批没有开放可写 sysctl 或模拟成功的安全控制接口。

## 网络选择

根网络使用[原生直连](direct-network.md)。每个 guest 会话拥有自己的 socket
和资源对象，普通 TCP/UDP 使用宿主网络，不经过 10.0.0.1 网关。这不产生独占
宿主 IP/端口空间；多个会话仍会竞争同一个宿主监听端口。

未来 `CLONE_NEWNET` 创建 guest 内独立网络对象，已有 socket 保持创建时的
namespace 归属，setns 只影响后续创建。私有 loopback、端口空间、veth/bridge
和可选发布端口在这一层实现，根网络无需再套 NAT。现有显式
`--virtual-network` packet fabric 可作为基础，但不是 `CLONE_NEWNET`。

## 后续顺序

1. signalfd 与信号/事件等待，随后 User namespace、IPC/net namespace；用真实
   容器工具探测结果排序，未知限制明确报错。
2. 原版 guest runc/crun 的最小 rootfs、多个隔离进程和生命周期测试；之后
   扩展完整容器引擎、overlayfs、cgroup 控制器及更广的安全接口。

## 验证

```sh
bash scripts/build-namespace-fixtures.sh
python3 tests/integration/namespaces.py
python3 tests/integration/direct-network.py
bash scripts/build-capability-fixtures.sh
python3 tests/integration/capabilities.py
bash scripts/build-cgroup-fixtures.sh
python3 tests/integration/cgroups.py
bash scripts/build-device-fixtures.sh
python3 tests/integration/devices.py
```

namespace 集成测试覆盖 1/4 CPU 下 UTS、chroot、mount 和 PID ABI，包括嵌套、
线程 exec、跨层信号、init 清理和 subreaper；再运行原版 Alpine BusyBox 的
unshare/nsenter/chroot/mount/umount/df/ps。
首批证据见[UTS/直连网络验证](../validation/guest-namespaces.md)，挂载批次见
[挂载隔离验证](../validation/guest-mounts.md)，本批见[PID 隔离验证](../validation/guest-pid.md)。

权限批次见[capability 与原版工具验证](../validation/guest-capabilities.md)，
cgroup/dumpable 批次见[成员生命周期与原版 crun 阻塞验证](../validation/guest-cgroups.md)。
设备与启动路径批次见[设备、openat2 与原版运行时验证](../validation/guest-devices.md)。
