# Guest 内容器：原版 crun rootful 运行

目标是在同一个 Trames guest 内运行原版 Linux 容器工具。容器共享解释器、
调度器和 guest 内核对象；当前不做宿主多实例管理器或自己的 OCI 启动器。
原版 Alpine `crun` 已通过下述 rootful 场景；使用方法见[容器入门](../guides/guest-containers.md)。
Docker 的守护进程、镜像与选定容器运行配置见[Docker 指南](../guides/guest-docker.md)。
原版 shim 所需的 OOM、管道权限和设备 BPF 已补齐；完整 OCI、rootless 与通用
Docker/Podman 兼容仍待扩展。

## 已实现

| 接口 | 当前范围 |
| --- | --- |
| `unshare` | `CLONE_NEWUTS`、`CLONE_NEWNS`、`CLONE_NEWPID`、`CLONE_NEWNET`、`CLONE_NEWIPC`、`CLONE_NEWCGROUP`、`CLONE_FS`、`CLONE_FILES`；未知组合与额度失败不改变调用者状态；NEWNS 隐含独立 fs context |
| `clone` | UTS/mount/PID/network/IPC/cgroup namespace；NEWNS 与 CLONE_FS、NEWPID 与 THREAD/PARENT 冲突返回 EINVAL；PID 1 不接受 CLONE_PARENT |
| `setns` | UTS/mount/PID namespace FD，类型 0 或对应 CLONE 标志；mount 拒绝共享 fs context；PID 仅接受当前或后代 namespace，影响后续子进程 |
| `uname`、`sethostname`、`setdomainname` | 调用者 UTS，名称最长 64 字节；按 guest CAP_SYS_ADMIN 授权 |
| namespace FD | `/proc/PID/ns/{uts,mnt,pid,pid_for_children,net,ipc,cgroup}`、task/TID 形式、dup、O_PATH、stat、nsfs statfs、`NS_GET_NSTYPE`；FD 保持 namespace 存活 |
| `chroot` | 独立 `(mount,inode)` fs.root，绝对路径/绝对符号链接从新根解析，根内 `..` 不能向上；保留 cwd/dirfd |
| `mount` | tmpfs、proc、sysfs、独立 devpts、bind/rbind、选定 remount；所有挂载初始为 private，支持 make-private/recursive-private |
| `umount2` | 普通卸载检查子挂载和 FD/cwd/root/exec/mmap 引用；MNT_DETACH 断开整个子树，已有能力仍能访问；UMOUNT_NOFOLLOW |
| `pivot_root` | 挂载根和 put_old 目录/祖先关系检查，旧根接到新根下，同 namespace 内位于旧根的 fs.root/cwd 一并切换 |
| 挂载视图 | `/proc/PID/{mounts,mountinfo}` 来自实际挂载树，按该进程 chroot 过滤/显示路径并转义字段；statfs/statx MNT_ID/fdinfo 使用实际挂载身份 |

## PID 层级与生命周期

调度器保留稳定的全 guest 任务编号，只在系统调用、信号信息和 proc 边界转换。
每个子 namespace 独立分配 PID，祖先可以看见后代，反向与兄弟之间不可见。
`unshare(CLONE_NEWPID)` 和 PID `setns` 设置 `pid_for_children`，调用者本身不迁移；
进入新 namespace 的首个子进程成为 PID 1，继承的外部 PGID/SID 显示为 0，
可随后调用 setsid 建立自己的会话。
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
权限层级、完整 ptrace 仍缺；seccomp 已有下述经典 BPF 子集。不冒充宿主 Linux 权限，也不更改 iPad 凭据。

原版签名 Alpine capsh 2.78 已可查询/丢弃/保留能力并测试 ambient/NNP 跨 exec。
crun 1.23.1 现在通过单核/四核下的选定生命周期验收：实际 shell 输出和退出码 7、
PID 1、独立 UTS/private mount namespace、proc 挂载，以及 create/start/state/exec、
TERM trap、stopped 和 delete。原版签名 APK 在 guest 内安装，OCI JSON 只是原版
运行时的测试输入。使用 cgroupfs manager；pause/resume、冻结后 force delete、两个完整 Alpine rootfs
并行运行、私有 devpts/临时设备与 shm、前台终端和 OCI pids.limit 已通过 1/4 CPU
验收。没有验证完整 OCI 或 Docker/Podman。
open_tree、keyctl、pidfd_open 等探测仍由原版运行时自行回退。

## 信号描述符与待处理信号

signalfd4 支持新建/更新 mask、NONBLOCK/CLOEXEC、dup/fork/exec 和 proc fdinfo。
read/readv 返回完整的 128 字节记录；无信号时阻塞或 EAGAIN，已有记录后短读，
小于一条记录返回 EINVAL。与 Linux 一样，复制故障发生前已出队的信号被消费；
前面的完整记录仍作为短读返回。SIGKILL/SIGSTOP 从 FD mask 中去除。

线程拥有私有 pending 队列，同一线程组共享进程 pending。fork 清空两种队列，
CLONE_THREAD 共享进程队列，exec 保留本线程与进程 pending。kill、SIGCHLD 和
普通 POSIX timer 进入进程队列；tkill/tgkill、线程定向 timer 进入私有队列。
普通信号合并并保留首条信息，实时信号按号码优先级/FIFO 排队；消费时优先私有
队列，再读共享队列，保持 Linux 对同步类信号号码的优先级。sigwait、普通 handler
与 signalfd 消费同一来源，proc 分别显示 SigPnd/ShdPnd。

read、poll/ppoll、pselect、epoll 的 level/ONESHOT readiness 使用当前调用线程的
私有与共享 pending；阻塞 read 保留 EINTR/SA_RESTART 行为。队列只由现有内核
协调线程访问，没有给 CPU 或原生访存增加锁。一个 signalfd 不占有信号，关闭 FD
不会丢弃 pending，也不会把别的进程的信号转交给继承此 FD 的子进程。

范围仍有限：实时队列使用固定 4096 条预算，标准信号尤其 KILL 不被该预算挤掉；
没有实现每 UID 的 RLIMIT_SIGPENDING 或 rt_sigqueueinfo/rt_tgsigqueueinfo。
SIGCHLD 的 CPU 时间字段保持零。epoll 仍按调用者快照检查 readiness，尚未模拟
Linux 跨 fork 继承 epoll 时绑定原注册进程信号等待队列的细节；子进程应新建并注册
epoll。PTY/嵌入终端已支持 EPOLLET，按输入/输出队列代次和 readiness 检查；
重复 wait 不重复通知，排空后同样长度的新数据不会丢边缘，MOD/ONESHOT 可重新启用。
Unix socket、pipe/FIFO、eventfd 和 NETLINK_ROUTE 回复队列也已支持 EPOLLET。
宿主 TCP/UDP、timerfd/signalfd 的 ET、EXCLUSIVE/嵌套 epoll 仍未实现。

## 设备节点与容器路径

mknodat 可创建字符/块设备 inode，需要 CAP_MKNOD，并执行目录权限、umask、
只读挂载检查。null/zero/full/random/urandom/tty/ptmx/kmsg 按 Linux 设备号
复用 guest 驱动；未注册的设备号可以 stat、O_PATH 查询和持久化，数据打开返回
ENXIO。不打开宿主设备，也不提供块驱动。独立 devpts 见下文。
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
mount/umount 的 proc FD 目标也使用实际 mount/inode 能力；挂载调用刷新 proc，
避免 FD 复用、目标改名或同名重建时跟错显示路径。

新 PID namespace 的 PID 1 继承父 PGID/SID/控制终端，不提前建立新会话。
祖先组/会话编号在新 namespace 内显示为 0，首次 setsid 可建立编号 1 的会话。
这一行为按 Linux v6.12 `copy_process` 校正，普通会话首领再次 setsid 仍返回 EPERM。

## 容器终端和描述符传递

新 devpts 挂载拥有自己的 inode 表、slave 编号、uid/gid/mode/ptmxmode/max 选项；
接受 newinstance，现代 Linux 语义下新挂载均独立。bind 和 namespace 复制保留
实例身份。每个实例从 /dev/pts/0 开始，调度器内部终端身份保持唯一，避免控制
终端混淆。最后一个 master 关闭时删除对应 slave 节点；旧 FD 不跟随编号重用。
根会话原有终端不出现在新实例中。TIOCGPTPEER 使用 master 保留的实例，即使
调用者 chroot 或 lazy unmount 后仍能打开同一 slave。会话总 PTY 预算仍为 4095。

Unix socketpair 的 stream/datagram/seqpacket 支持 SCM_RIGHTS，传递相同 open
file description，保留共享偏移、状态和 inode/PTY 生命周期。支持 MSG_PEEK、
MSG_CMSG_CLOEXEC、控制区/FD 表不足的 MSG_CTRUNC、部分 stream 读取时的辅助
消息边界。关闭接收端或不接收辅助消息时释放未接收的描述符，发送方关闭不会
提前销毁队列中的文件。每次最多 253 个 FD，消息队列仍有字节/条目额度。

暂不传递 AF_UNIX socket FD：需要实现 in-flight 图的循环垃圾回收，当前明确
返回 EOPNOTSUPP。普通文件、pipe、PTY、namespace 等句柄可传递。具名/抽象
AF_UNIX stream/seqpacket 已有，SCM_CREDENTIALS 尚缺；外部 console-socket 场景未验收，前台
crun 内部 socketpair 终端通道已验收。

## Docker 所需的进程与本地通信

AF_UNIX stream/seqpacket 支持 filesystem/abstract bind、listen、connect、accept4、
getsockname/getpeername 和 SO_PEERCRED。路径使用 guest inode、DAC/umask；抽象
地址保留内部 NUL，最后一个 endpoint 引用关闭后释放名字。backlog 有限，满时
阻塞重试或非阻塞 EAGAIN；EMFILE 不消费待接受连接。路径 unlink 与活跃连接独立，
过期 inode 返回 ECONNREFUSED。具名 datagram 的定址发送尚缺。
SO_REUSEADDR/REUSEPORT 保存请求值，不允许复用已绑定 Unix 名称。
队列代次驱动 ET，peek/失败读取不产生假写事件；普通文件注册 epoll 返回 EPERM。
FIFO 经 O_PATH 与 `/proc/self/fd` 重新打开会按新 flags 建立独立管道端点，
保留 unlink 后的 inode，正确计数 reader/writer、等待对端和释放 EOF。

PR_SET/GET_PDEATHSIG 记录实际创建线程，在线程退出时投递进程信号并保留发送者
身份；fork 清零，普通 exec 保留，effective/fs UID/GID 变化清零，孤儿继续采用
既有 subreaper/init 规则。线程总额度从 32 提升到 256，threads-max/rlimit/proc
一致；不是预分配线程。Go 守护进程的 ELF 输入上限提升为 256 MiB，映射预算仍
默认每地址空间 512 MiB / 全会话 1 GiB，现在可通过运行时、CLI 和
[iPad 设置](../guides/guest-memory.md)调整。

每个 guest 根网络具有唯一的 `ns/net` 对象。1.0.0 后开发分支可创建独立 network、
IPC、cgroup namespace，并通过 FD、task/TID 路径、nsfs bind mount 与 setns 保持
真实引用；不改变宿主 namespace。隔离与默认 seccomp 验收见[记录](../validation/container-isolation.md)。
UTS/PID 句柄也可 bind；mount namespace 句柄的挂载循环回收尚缺，明确拒绝。
新 network namespace 只有独立的 lo；NETLINK_ROUTE/ioctl 可查询并改变它的 up/down，
并检查 CAP_NET_ADMIN。根直连接口只接受原有的幂等 lo up。veth/bridge、地址、
路由与 MTU 配置仍待实现；[后续路线](container-network-design.md)包含实际过滤/NAT。
private mount 请求 MS_SLAVE/REC 时继续保持 private（没有 shared master）；
这不代表已实现跨 namespace 挂载传播。

12 个 xattr syscall 已有路径/链接/FD 变体，持久化 `user.*`；缺失的文件 capability
与 ACL 查询返回 ENODATA，设置返回 EOPNOTSUPP，详见[存储](storage.md)。

`/proc/PID/oom_score_adj` 和 task/TID 变体支持读取和写入 -1000..1000，
权限、最低值与 CAP_SYS_RESOURCE 检查；fork 复制、线程共享、exec 保留。
打开文件持有弱任务引用，进程回收后返回 ESRCH，避免旧 FD 指向新任务。
支持 Linux 的数值解析、偏移和短写规则；与原生 Linux 的独立 C 夹具比对。
这是 OOM 偏好存储，不实现 OOM killer，也不修改宿主策略；非线程 CLONE_VM
进程间的 Linux shared-mm 传播特例尚缺。匿名 pipe 的两个端点共享实际 inode
权限与所有者，fchmod/fchown/fstat、dup/fork 与描述符传递保持同一元数据。

## Cgroup 设备 BPF 子集

`bpf(280)` 支持 CGROUP_DEVICE 的 LOAD、ATTACH、DETACH、GET_FD_BY_ID、QUERY。
支持 ALLOW_MULTI 和原子 REPLACE、程序 FD 关闭后的挂载生命周期、有效规则查询。
验证器只接受有限的向前控制流、12 字节设备上下文、MOV/AND/RSH、相等/不等跳转
及 EXIT；未初始化寄存器、越界访问、循环、stack、helper、map 和其他程序类型
均拒绝。程序在 VFS 内解释，不提交给宿主内核，不使用 JIT。

设备 open、proc FD 重开、PTY peer 打开和 mknod 真正执行规则；祖先和本组的
程序全部允许才放行，已有 FD 保持原有访问能力。规则随 cgroup 成员迁移生效，
fork 继承成员关系。要求 guest CAP_SYS_ADMIN；最多 128 个存活程序，每个
4096 条指令、每组 16 个挂载。ALLOW_OVERRIDE、网络 BPF 等仍不支持。
只有 syscall 边界增加设备检查，没有新增 CPU/guest memory 热路径锁。

crun 对空 OCI `blockIO: {}` 也会打开 `io.max`。非根组提供空的无限制视图，
允许打开/读空，但任何非空限速写入返回 EOPNOTSUPP；不公布 io 控制器或假装
执行资源限制。`mqueue` 可挂载同一 guest IPC 范围内的空文件系统，具有独立
inode/statfs 身份，禁止用普通文件操作创建队列。`mq_*` 仍为 ENOSYS，尚未支持
POSIX 消息队列或独立 IPC namespace。

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

cgroup.freeze 保存本组请求，cgroup.events 的 frozen 包含祖先冻结状态。调度器在
CPU worker 全部返回后停止分发被冻结的任务，保留阻塞调用、SIGSTOP 和待处理信号。
解冻或迁出冻结树后继续；普通信号等待解冻，SIGKILL 与 cgroup.kill 可直接终止。
cgroup.kill 对写入时的整个后代树记录终止请求；同一并行批次尚未提交的 fork
在 KILL 前不会再产生逃逸子进程。没有借用宿主进程/信号设施。

pids 是当前唯一公布的 controller。通过 cgroup.subtree_control 的 +pids/-pids
为子组安装/移除 pids.current/max/peak/events/events.local，旧控制 FD 失效且不
跟随同名重建。fork/clone 同时检查所有启用的祖先限额，线程也占一个名额，超限
返回 EAGAIN；退出释放名额，迁入/降低限额允许 current 超过 max，已有任务不被杀。
peak、限制组及其祖先的 events 统计来自实际成员/拒绝事件；失败的子级预检查可以
提高 peak，与 Linux 的宽松峰值含义一致。max 支持 max 或 0..4194304。整个 guest
有 256 个任务的总额度；该总额度失败不会伪计为某组 pids 限额事件。

这是 domain 子集：没有 CPU/memory/io 配额、threaded 模式、事件文件 poll 通知、
cgroup namespace 或非 root 委派。非根内部组启用子控制器时不得有任务；迁入这样
的组返回 EBUSY。组管理和迁移要求 CAP_SYS_ADMIN；节点由 root 持有，不开放
chmod/chown 委派。资源和冻结检查只在 syscall/调度边界执行。

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
`MS_STRICTATIME` 清除 relatime，普通读取更新 atime；`MS_SILENT` 和旧版 mount
magic 被兼容。不支持的挂载行为明确返回错误。

这些对象只在现有内核调度边界访问，没有给普通 CPU 指令/访存增加锁或权限检查。
挂载点使用缓存路径验证，祖先改名后才重新查找 inode，避免每次 lookup 扫描磁盘树。

## 配额与尚缺的部分

- 每个 guest 会话最多 128 个存活挂载对象，含 namespace 副本和被 FD/映射保留的
  挂载；耗尽 ENOSPC，释放后可再创建。UTS 和 PID 谱系各有 128 个存活对象额度。
  PID 最多嵌套 32 层，每会话最多 256 个任务；编号不循环复用，fork 前回收无引用映射。
- tmpfs 独立于磁盘 manifest，默认 16 MiB / 1024 inode；`size` 可设到 64 MiB，
  按需分配，`nr_inodes` 可降低节点上限，`mode` 设置根目录权限。
  卸载/会话结束后内容不持久化。这里的内存上限
  与根磁盘默认 16 GiB 预算无关；尚未接入统一的 tmpfs/guest RAM 回收机制。
- 所有挂载均为 private；shared/slave/unbindable 传播、MS_MOVE、overlayfs、块设备
  文件系统及 open_tree/move_mount/fsopen 等新挂载 API 尚缺。
- procfs 支持有权限检查的 `oom_score_adj` 读写；其余 proc 控制端点与 sysfs 保持只读。独立 devpts 支持初始挂载选项，重挂载修改
  uid/gid/mode/max 尚未实现，明确返回 EOPNOTSUPP。
- 跨进程 `/proc/PID/root`、cwd 等完整 magic-link 能力、user namespace、
  IPC/net namespace、CPU/memory/io 资源控制、委派、seccomp
  仍缺。当前运行范围为文档配置的 rootful 容器，不构成完整安全隔离边界。

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

1. 按真实工具缺口扩展 user/IPC/net namespace、具名 AF_UNIX 和更广的 epoll 事件源。
2. 再扩展完整容器引擎、overlayfs、CPU/memory/io 控制器和安全接口。

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
bash scripts/build-signalfd-fixtures.sh
python3 tests/integration/signalfd.py
python3 tests/integration/containers.py
```

namespace 集成测试覆盖 1/4 CPU 下 UTS、chroot、mount 和 PID ABI，包括嵌套、
线程 exec、跨层信号、init 清理和 subreaper；再运行原版 Alpine BusyBox 的
unshare/nsenter/chroot/mount/umount/df/ps。
首批证据见[UTS/直连网络验证](../validation/guest-namespaces.md)，挂载批次见
[挂载隔离验证](../validation/guest-mounts.md)，本批见[PID 隔离验证](../validation/guest-pid.md)。

权限批次见[capability 与原版工具验证](../validation/guest-capabilities.md)，
cgroup/dumpable 批次见[成员生命周期与原版 crun 阻塞验证](../validation/guest-cgroups.md)。
设备与启动路径批次见[设备、openat2 与原版运行时验证](../validation/guest-devices.md)。

信号与原版容器生命周期批次见[signalfd/crun 验证](../validation/guest-signalfd.md)。
