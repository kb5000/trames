# 当前 CPU 指令与 Linux 系统调用缺口（0.8 后续源码）

审计日期：2026-10-12。运行时代码基于已验证的
[0.8.0/build 9](https://github.com/kb5000/trames/tree/5d6f1076297385a0d120ff68b02a8c759d3a0db2)。
本页已纳入后续文件接口与系统日志实现，并重新生成清单和探测结果。
这些后续源码尚未进入已有 IPA，将并入下一次合并更新；具体范围见
[实现与验证记录](../validation/compatibility-log.md)。
旧版审计保留各自的历史状态；本页作为当前剩余缺口的入口。

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
当前有 **138 个已实现子集、178 个未实现条目、2 个废弃条目**。
本轮使用实际 ARM64 构建，在唯一缓存解释器上逐个调用全部 180 个未分派编号，
均返回 `-ENOSYS`（-38）。这验证的是无入口行为，不能代替已有入口的 ABI 完整性测试。

完整的编号、名称、状态、当前源码位置和探测返回值见
[系统调用 CSV](../validation/compatibility-current-syscalls.csv)；
单后端、统计及源码指纹见 [审计 JSON](../validation/compatibility-current-syscalls.json)。
可运行 `python3 scripts/audit-syscalls.py` 更新当前报告，历史报告不会被默认覆盖。

以下按用途列出主要缺口；每行列举代表接口，完整 178 项以 CSV 中 `missing` 行为准。

| 用途 | 尚未实现的代表接口 |
| --- | --- |
| 文件元数据与路径 | openat2、fchmodat2、execveat；set/get/list/remove xattr 的路径、链接及 fd 变体 |
| 文件数据与批量 I/O | preadv2/pwritev2、sendfile、splice/vmsplice/tee、copy_file_range、fallocate、syncfs、sync_file_range、readahead、fadvise64 |
| 文件通知和异步 I/O | inotify、fanotify、旧 AIO、io_uring、signalfd4 |
| 内存 | mremap、msync、madvise、mincore、memfd_create/secret、mlock/munlock 系列、userfaultfd、mseal、内存策略与 pkey 系列 |
| 进程与调度 | clone3、waitid、pidfd 系列、sched 参数/亲和性、get/setpriority、getrusage、times、getcpu、rseq、membarrier |
| 信号与同步 | sigaltstack、rt_sigqueueinfo/rt_tgsigqueueinfo、get/set_robust_list、futex_waitv、独立 futex_wait/wake/requeue、restart_syscall |
| IPC 与网络批处理 | SysV shm/sem/msg、POSIX mq、sendmmsg/recvmmsg |
| 内核管理与隔离 | mount/umount/pivot_root 及新挂载 API、chroot、unshare/setns、capget/capset、ptrace/seccomp、process_vm_readv/writev、bpf/perf、landlock、模块/重启/交换分区等 |

2 个废弃条目是 `lookup_dcookie`（18）与 `nfsservctl`（42），同样返回 ENOSYS，
不计入 178 个未实现条目。内核管理类接口需要结合用户态虚拟化目标选择范围，
不能把完整内核功能视为普通文件/进程接口的自然延伸。

本轮新增 statx、preadv/pwritev、close_range 和 syslog；它们仍按已实现子集标注。
`dmesg` 可读取当前会话的未实现调用和 CPU 故障；`/dev/kmsg` 提供独立读取游标，
具体边界及命令见[系统日志记录](../validation/compatibility-log.md)。

## Linux：已有入口但语义仍有缺口

| 已有接口或设施 | 仍缺少的行为 |
| --- | --- |
| statx / syslog | statx 不报告未有依据的 birth time、DIO 对齐等可选字段；syslog 不接管宿主内核日志或控制台 |
| mmap / 文件映射 | 共享文件 MAP_SHARED、一致写回、超过 EOF 的 SIGBUS 语义；目前支持已验证的私有文件映射和匿名映射 |
| rt_sigaction / 信号帧 | SA_ONSTACK 在未启用备用栈时使用普通栈；sigaltstack 注册、完整实时排队、SVE/SME 信号上下文仍缺失；同步 CPU 故障恢复已有实现 |
| socket / socketpair | Unix socketpair 已实现；具名 AF_UNIX 端点、SCM_RIGHTS 等辅助消息传递仍缺失 |
| epoll | level-triggered 和 ONESHOT 已实现；EPOLLET、嵌套 epoll、EXCLUSIVE 不支持 |
| timerfd | 普通 realtime/monotonic/虚拟 boottime 定时器已有实现；alarm 时钟和 CANCEL_ON_SET 不支持 |
| fcntl / futex | POSIX 记录锁已有实现；OFD 锁、完整 robust/shared futex 语义仍缺失 |
| mknodat / mount / 虚拟设备 | 普通 inode 和 FIFO 已有实现；动态设备节点、真实挂载和完整 Linux 驱动模型不支持；现有 /dev、/proc、/sys 为虚拟端点 |
| PTY / termios | 多 PTY 和非零 VTIME 不支持；不能以默认 Vim/htop 已运行推断全部终端模式 |
| ELF / 资源限制 | 重叠 PT_LOAD 页面不支持；映射预算每进程 512 MiB、所有地址空间合计 1 GiB，guest fd 表 256 项，并非完整 Linux 资源模型 |

已实现子集的 flags、地址族、错误路径和并发行为仍应按具体实现与验证记录核对。
M6 的具体边界见 [能力记录](compatibility-m6.md)与[程序验证](../validation/m6.md)。
后续完善可优先按实际程序需求补文件元数据/批量 I/O、共享映射与 memfd、备用信号栈、
Unix 具名端点/FD 传递和文件通知，再扩展内核管理类范围；本页只记录现状，不新增实现承诺。
