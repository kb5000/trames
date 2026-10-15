# 当前 CPU 指令与 Linux 系统调用缺口（开发分支）

系统调用审计更新：2026-10-15；CPU 编码清单沿用 2026-10-13 的结果。运行时代码基于已验证的
[0.8.0/build 9](https://github.com/kb5000/trames/tree/5d6f1076297385a0d120ff68b02a8c759d3a0db2)。
本页已纳入后续文件接口与系统日志实现，并重新生成清单和探测结果。
0.9.0/build 10 已通过 Linux / Apple CI 和七项 iPad 模拟器测试，安装包及证据见[发布记录](../validation/release-0.9.md)；具体范围见
[系统日志记录](../validation/compatibility-log.md)和[共享映射与 ABI 扩展](../validation/mmap-coverage.md)。
旧版审计保留各自的历史状态；本页作为当前剩余缺口的入口。

[Node.js 兼容与内存模型](../architecture/nodejs-memory.md)补充了匿名私有
PROT_NONE 地址预留、提交计费、argv/env 排布、musl 信号标志和标准流非阻塞行为。
TCP 保活选项属于已有 setsockopt/getsockopt 的子集扩展；虚拟网络的探测间隔和
次数仍是兼容参数，不等同于 Linux 的完整超时策略。系统调用入口数量保持不变。

[多核执行](../architecture/multicore.md)已实现宿主 CPU 工作线程、共享访存事务、
ARM 独占/原子/acquire-release/屏障及共享 futex 键；内核和 NativeCall 初期仍集中处理。
采用 ARM 允许结果的较强子集，不复现所有弱内存交错；普通读写之间仍允许竞争。
这次扩大已有接口语义，系统调用入口数与指令审计样本数保持不变。

## CPU：尚未实现与兼容替代

范围是 AArch64 EL0 用户态。应用只保留一个 Rust 缓存解释器；计算优先使用
静态 ARM64 原生指令，不使用 JIT。x86_64 只是开发验证宿主，不是 guest 架构。

| 范围 | 当前缺口 |
| --- | --- |
| SVE / SVE2 | 尚未实现 Z/P/FFR 状态、可变向量长度及其算术、谓词、访存指令；Linux 向量长度控制和信号上下文也未实现 |
| SME | 尚未实现流式模式、ZA 状态及矩阵指令；SMSTART、SMSTOP、ZERO {ZA} 仍不支持 |
| PAC / BTI | 已接受已列出的指令和真实跳转/访存，但不做签名认证和落点保护；不是完整安全扩展 |
| MTE | 已实现零标签兼容模式及地址计算、清零、写回；没有真实分配标签、标签错误检测和对应 Linux 控制语义 |
| LS64 / WFxT / LSE128 | 已有选定功能：普通内存的 64 字节传输、允许提前唤醒的等待、成对原子操作；不宣称完整硬件设备内存、时序或扩展能力 |
| 浮点状态与边界数值 | 原生运算优先；可选扩展回退的舍入、NaN 载荷和异常标志不保证与所有 ARM 实现逐位一致，按当前速度优先策略保留 |
| 未纳入样本的可选扩展和编码 | 没有完整 ISA 穷举证据，不能把没有探测的编码视为已支持；未来扩展需逐项补充状态、解码和语义验证 |

特权块标签指令 LDGM/STGM/STZGM 仍触发 SIGILL，属于正常 EL0 范围以外。
EL1/EL2、MMU、启动真正 Linux 内核、AArch32 不属于本项目当前用户态范围。

本轮重新运行 [指令审计脚本](../../scripts/audit-instructions.py)：893 条代表编码中，
877 条成功解码，2 条进入 SVC，2 条产生已支持的同步异常，12 条不支持。
这 12 条包括 6 条 SVE/SVE2 样本、3 条 SME 样本和上述 3 条特权标签指令。
**样本数量不是 ISA 覆盖率，也不是所有缺失编码的数量。**
逐编码结果见 [CSV](../validation/instruction-coverage-current.csv)，
能力位与源码指纹见 [JSON](../validation/instruction-coverage-current.json)。

CSSC、BF16、FHM/复数、SHA512/SHA3/SM3/SM4、MOPS、LOR 和选定 LSE128 操作
已从“完全未实现”清单中移除。同步 CPU 故障的 guest 信号处理、上下文修改与恢复
也已实现。已有范围及兼容替代的具体指令见
[0.8 CPU 覆盖与语义验证](../validation/instruction-coverage.md)。

## Linux：尚无入口的系统调用

固定参照仍为 Linux v6.12 原生 AArch64 的 318 个编号，不是最新版内核的全部接口。
当前有 **190 个已实现子集、126 个未实现条目、2 个废弃条目**。
本轮使用实际 ARM64 探测 ELF，在唯一缓存解释器上逐个调用全部 128 个未分派编号，
均返回 `-ENOSYS`（-38）。这验证的是无入口行为，不能代替已有入口的 ABI 完整性测试。

完整的编号、名称、状态、当前源码位置和探测返回值见
[系统调用 CSV](../validation/compatibility-current-syscalls.csv)；
单后端、统计及源码指纹见 [审计 JSON](../validation/compatibility-current-syscalls.json)。
可运行 `python3 scripts/audit-syscalls.py` 更新当前报告，历史报告不会被默认覆盖。

以下按用途列出主要缺口；每行列举代表接口，完整 126 项以 CSV 中 `missing` 行为准。

| 用途 | 尚未实现的代表接口 |
| --- | --- |
| 文件数据与批量 I/O | splice/vmsplice/tee、fallocate、sync_file_range |
| 文件通知和异步 I/O | inotify、fanotify、旧 AIO、io_uring |
| 内存 | memfd_secret、mlock/munlock 系列、userfaultfd、mseal、内存策略与 pkey 系列 |
| 进程与调度 | clone3、pidfd 系列、实时调度、sched_rr_get_interval、sched_setattr/getattr、get/setpriority、getrusage、times、rseq |
| 信号与同步 | rt_sigqueueinfo/rt_tgsigqueueinfo、get/set_robust_list、futex_waitv、独立 futex_wait/wake/requeue、restart_syscall |
| IPC 与网络批处理 | SysV shm/sem/msg、POSIX mq、sendmmsg/recvmmsg |
| 内核管理与隔离 | 新挂载 API、传播挂载/overlayfs、ptrace/seccomp、process_vm_readv/writev、perf、landlock、模块/重启/交换分区等 |

2 个废弃条目是 `lookup_dcookie`（18）与 `nfsservctl`（42），同样返回 ENOSYS，
不计入 126 个未实现条目。内核管理类接口需要结合用户态虚拟化目标选择范围，
不能把完整内核功能视为普通文件/进程接口的自然延伸。

先前的 statx、preadv/pwritev、close_range 和 syslog 已扩展为本轮包含 23 个新入口的
[内存/文件/调度实现](../validation/mmap-coverage.md)；所有接口仍按已实现子集标注。
`dmesg` 可读取当前会话的未实现调用和 CPU 故障；`/dev/kmsg` 提供独立读取游标，
日志包含限长转义的进程 `comm`；新增接口见[兼容性补充](../validation/runtime-compat.md)。具体边界及命令见[系统日志记录](../validation/compatibility-log.md)。

`execveat` 已加入入口，支持匿名 memfd、取消链接的文件、`AT_EMPTY_PATH`、
目录 fd 相对路径和选定 `O_PATH` 引用。执行按 inode 处理 `/proc/self/fd` 和
`/proc/self/exe`；`(deleted)` 是显示标记，单独出现不阻止执行。Python 复现和
23 项原生 Linux 对照见[fd 执行验证](../validation/fd-exec.md)。

首批新增 chroot、unshare、setns、sethostname、setdomainname 五个入口；
UTS 对象和 namespace FD 具备实际共享/隔离及保活语义，clone 增加 NEWUTS。
后续增加 mount/umount2/pivot_root、mount namespace、tmpfs、bind/rbind，以及 waitid、child subreaper、PID namespace 层级、init 生命周期和 proc 挂载视图。后续新增 capget/capset 和 capability prctl 子集，接入实际权限判断及 UID/exec 转换；后续补 cgroup v2 域成员生命周期和 dumpable/敏感 proc 访问。本批增加设备 inode/持久化、openat2 路径约束、proc FD chmod/chown、同点 pivot_root，并校正 PID init 的会话继承。最新批次补齐 signalfd4、线程私有/进程共享信号队列和 proc FD 挂载目标，原版 crun 的选定启动、exec、TERM 和删除生命周期已通过；新批次通过 cgroupfs、pause/resume/kill、pids 限额、并发 Alpine rootfs 和独立 devpts 前台终端；补 SCM_RIGHTS 子集及 PTY EPOLLET。user/IPC/net namespace、CPU/memory/io 控制与委派仍未实现，详见[guest 内容器基础](../architecture/guest-containers.md)。
Docker 批次新增 12 个 xattr 入口、具名 Unix socket、PDEATHSIG 和更多 ET 来源。
后续增加 cgroup device BPF 入口与实际设备规则执行；补 proc OOM 偏好、匿名 pipe
权限、strictatime/64 MiB tmpfs、空 mqueue 挂载和明确拒绝限速写入的 io.max 视图。
默认网络改为[宿主原生直连](../architecture/direct-network.md)，不改变系统调用编号数。

## Linux：已有入口但语义仍有缺口

| 已有接口或设施 | 仍缺少的行为 |
| --- | --- |
| bpf / cgroup devices | 仅选定 CGROUP_DEVICE 向前控制流与 LOAD/ATTACH/DETACH/GET_FD_BY_ID/QUERY；无 map/helper/stack/loop、网络程序、JIT、ALLOW_OVERRIDE 或 host BPF |
| proc OOM / mqueue / io.max | OOM 偏好不含 victim selector 或非线程 shared-mm 传播；mqueue 仅空挂载视图，mq_* 仍缺；io.max 可读空但非空写入 EOPNOTSUPP，无 IO 限速能力 |
| namespace / chroot | UTS/mount/PID、fs/files unshare、fs.root、tmpfs/bind/umount/pivot_root 已有；proc 按挂载绑定 PID 视图，NSpid 等显示层级；当前根网络 ns/net 句柄、bind/setns 已有，独立 net/user/IPC、跨进程 proc root/cwd magic link 尚缺；cgroup2 支持域成员、freeze/kill 和 pids 限额，其他控制器/委派仍缺；原版 crun 选定生命周期已通过，完整 OCI/容器引擎仍未验收 |
| capability / prctl | 初始 guest user namespace 的 E/P/I/bounding/ambient、securebits、no_new_privs、dumpable 0/1 与选定 proc 访问检查已有；file capability xattr、user namespace 映射/权限层级、完整 ptrace、core 文件写出、seccomp 仍缺 |
| xattr | 路径/链接/fd 的 12 个入口及持久化 user.* 已有；security.capability/ACL 设置仍明确拒绝 |
| statx / syslog | statx 不报告未有依据的 birth time、DIO 对齐等可选字段；syslog 不接管宿主内核日志或控制台 |
| mmap / 内存管理 | 已有共享文件/匿名映射、写回、EOF/truncate SIGBUS、选定 remap/discard；仍缺需求分页/回收、MAP_SYNC、huge pages、DONTUNMAP、跨不同映射段 remap 等 |
| memfd / 文件接口 / 调度 | memfd 的新 sealing/huge/no-exec 模式、更多 RWF flags、实时策略仍缺失；openat2 已支持路径/链接/跨挂载约束，CACHED 返回 EAGAIN；已支持 1–32 个虚拟 CPU、实际宿主并行、affinity 与 getcpu |
| rt_sigaction / 信号帧 | 备用信号栈、嵌套及 AUTODISARM 已实现；kill/tkill/tgkill 的实时排队已有；rt_sigqueueinfo/rt_tgsigqueueinfo、每 UID 的 RLIMIT_SIGPENDING、SVE/SME 信号上下文仍缺失；同步 CPU 故障恢复已有实现 |
| socket / socketpair | Unix socketpair 已实现；SCM_RIGHTS 支持文件/pipe/PTY 等 FD，具名/抽象 stream/seqpacket 已有；Unix socket FD 的循环回收、具名 datagram 定址发送与 SCM_CREDENTIALS 仍缺 |
| signalfd4 / epoll | 128 字节 read/readv、私有/共享队列、level-triggered 和 ONESHOT 已实现；PTY/嵌入终端、Unix socket、pipe/FIFO、eventfd、Netlink EPOLLET 已有；宿主 socket 和 timerfd/signalfd 的 EPOLLET、嵌套 epoll、EXCLUSIVE、跨 fork 继承 epoll 的 Linux 信号等待队列绑定仍缺，子进程应新建并注册 epoll |
| timerfd | 普通 realtime/monotonic/虚拟 boottime 定时器已有实现；alarm 时钟和 CANCEL_ON_SET 不支持 |
| fcntl / futex | POSIX 记录锁已有实现；OFD 锁、完整 robust/shared futex 语义仍缺失 |
| mknodat / mount / 虚拟设备 | 普通/FIFO/字符/块 inode 与设备号持久化已有；标准字符设备复用 guest 驱动，未注册设备打开 ENXIO；socket inode 由 bind 创建，socket mknod 仍缺；独立 devpts 已有；块驱动/文件系统和完整 Linux 驱动模型仍缺 |
| PTY / termios | 已支持独立多 PTY、控制终端、关闭/hangup、原版 sudo 默认 use_pty；非零 VTIME 和更多驱动 ioctl 仍不支持，见[sudo 验证](../validation/sudo-timezone.md) |
| membarrier / I/O 优先级 | 已有 GLOBAL、PRIVATE_EXPEDITED、注册查询和单任务 ioprio_get；SYNC_CORE/RSEQ 屏障、ioprio_set 和组/用户优先级查询仍缺失 |
| ELF / 资源限制 | 重叠 PT_LOAD 页面不支持；映射预算每进程 512 MiB、所有地址空间合计 1 GiB，guest fd 表 256 项，RLIMIT_FSIZE 已有软硬限制与写入/截断约束，其他资源修改及其他 PID 的 prlimit 仍不完整 |

已实现子集的 flags、地址族、错误路径和并发行为仍应按具体实现与验证记录核对。
M6 的具体边界见 [能力记录](compatibility-m6.md)与[程序验证](../validation/m6.md)。
后续完善可优先按实际程序需求补文件元数据/批量 I/O、更多内存操作、
Unix 具名端点/FD 传递和文件通知，再扩展内核管理类范围；本页只记录现状，不新增实现承诺。

## NativeCall 开发

[NativeCall v1 pure profile](../architecture/accelerator-v1.md) 使用显式宿主启用、
私有 auxv 协商和固定 guest 桩库协议。它不是 Linux 系统调用或普通 AArch64 ISA
扩展，不增加本页的已实现系统调用/CPU 样本数；默认关闭时 HLT 保留原有异常行为。
0.10.0/build 11 已包含完整混合版 musl 和这些后续接口；
[安装包和验证](../validation/release-0.10.md)。

0.10.0 的[完整混合版 musl](../architecture/musl-hybrid.md)保留全部原公开符号和标准加载器路径，
只对五个纯函数接入 NativeCall，其他 musl 函数保留原始 guest 实现。
完整库的交付不改变本页的 CPU 样本或 Linux 系统调用覆盖数。
