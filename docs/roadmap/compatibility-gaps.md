# CPU / Linux ABI 缺口清单（0.3.0）

这是已发布 0.3.0 的历史基线；尚未发布代码的后续状态见
[工作区剩余缺口](compatibility-gaps-worktree.md)。两份清单不能混作同一版本的能力。

审计日期：2026-10-11。代码基线：
[`00050665`](https://github.com/kb5000/trames/tree/00050665cfaf4868c5d21d6f4211d111e586059d)。
本次只整理和探测，未修改运行时代码，也不生成新的 IPA。

当前可以运行 Alpine/musl、ash、apk、默认 Vim 和已验收的 Python 工作流；
这不代表完整 AArch64 指令集或完整 Linux ABI。标为“已有子集”的接口仍可能
缺少某些 flags、命令、地址族或边界行为。

## 范围与原始清单

- CPU 按 AArch64 EL0 用户态指令族审计，分别列出基础 ARMv8-A、FP/AdvSIMD
  和可选扩展。不把 EL1/MMU、内核启动、AArch32 等算成用户态兼容缺口。
- 系统调用以 Linux **v6.12** 的
  [`scripts/syscall.tbl`](https://github.com/torvalds/linux/blob/v6.12/scripts/syscall.tbl)
  为固定编号参照，选择 `common,64,renameat,rlimit,memfd_secret`，与
  [arm64 的 ABI 配置](https://github.com/torvalds/linux/blob/v6.12/arch/arm64/kernel/Makefile.syscalls)
  一致。这是编号清单，包含废弃和依赖内核配置/架构功能的条目；不是要求全部实现的
  产品清单。没有计入 32 位 time64 接口、未分配编号或 6.12 之后的新调用。
- 已合并调度器、Linux/VFS 和网络三层分派；不能只查
  `trames-linux/src/lib.rs` 中保留的早期 write/exit 分派器。
- [系统调用逐项 CSV](../validation/abi-audit-syscalls.csv)：318 个编号的名称、
  状态、源代码入口和限制说明。
- [CPU 代表编码 CSV](../validation/abi-audit-cpu.csv)：317 条合法汇编编码，
  分别以 Rust 和 C++ 后端执行一个指令预算，记录解码结果。
- [审计元数据及复现片段](../validation/abi-audit.json)：基线、参照文件校验值、
  工具版本和两个确认的行为错误。

编号统计：**122 个调用提供实际功能子集；1 个入口明确不支持（socketpair）；
1 个入口错误分派（setdomainname）；194 个编号无入口，其中 2 个是内核表中
已废弃的空实现**。所以，排除废弃条目后，有 192 个调用没有分派入口，
另有上述 2 个不能算作已实现的调用。这不是兼容性百分比。

CPU 探测中 256 条编码报不支持，60 条成功解码，1 条进入 SVC；两种后端一致。
样本刻意集中在缺口，**不是从完整 ISA 随机抽样，也不是指令覆盖率**。
“成功解码”也不等于语义正确，下面的标量 SLI 就是反例。某个编码的结果
不自动证明同一助记符的所有宽度、标量/向量和地址模式都支持。

## 已确认的行为错误

| 项目 | 当前结果 | 应有行为与证据 |
| --- | --- | --- |
| `sync` / `setdomainname` 编号错配 | `sync` 的正确编号 81 返回 `ENOSYS`；编号 162 忽略参数并同步 VFS，返回 0 | AArch64 的 162 是 `setdomainname`，应设置虚拟域名或明确拒绝。见 [state.rs](../../crates/trames-linux/src/state.rs) 的 `162` 分支；两种后端实测确认 |
| 标量 `SLI Dd,Dn,#imm` | 被标量移位分支接受，但丢弃目的寄存器应保留的低位 | `D0=5,D1=4; SLI D0,D1,#1` 应得到 9，Rust/C++ 得到 8，独立 QEMU cortex-a53 得到 9。见 [floating.rs](../../crates/trames-cpu/src/floating.rs)；向量 SLI 是另一条实现路径，不能混淆 |

这两项应优先修正。当前文档中的“同步支持”不能据此推断 `sync(2)` 正确；
同样，M5 的向量位插入验收没有覆盖这个标量形式。

## CPU：基础指令与 FP/NEON 缺口

普通 32/64 位整数算术、位运算、条件选择、乘除、分支、地址生成，大部分常用
整数/SIMD 访存、单寄存器独占访问、TLS，以及 S/D 标量浮点已经存在。
S/D 浮点涵盖加减乘除、平方根、融合乘加、比较、舍入和整数转换；NEON 也有
数据搬运、结构访存、部分整数和浮点运算。**浮点并非尚未实现。**

| 缺口类别 | 未实现或仅部分实现的指令/形式 | 影响 |
| --- | --- | --- |
| 成对独占访存 | `LDXP/LDAXP/STXP/STLXP`，包括 W/X 形式 | 部分 128 位原子操作；单寄存器 `LDXR/STXR` 等已支持 |
| 非特权访存形式 | `LDTR/STTR` 及字节、半字、带符号变体 | 一般用户态二进制较少使用，但基础解码不完整 |
| NEON 重排和位运算 | `EXT`、向量 `REV16/REV32/REV64/RBIT`、`CNT/CLZ/CLS`、`NOT/BIC/ORN/BSL/BIT/BIF` | 字符串、哈希、压缩及自动向量化代码；`TBL/TBX`、`ZIP/UZP/TRN`、`AND/ORR/EOR` 已有实现 |
| NEON 整数乘法 | 向量和按 lane 的 `MUL/MLA/MLS`；`SMULL/UMULL/SMLAL/UMLAL/SMLSL/UMLSL` 及上半区形式 | 普通编译器向量化和数值代码 |
| NEON 加宽算术 | `SADDL/UADDL/SADDW/UADDW/SSUBL/USUBL/SSUBW/USUBW` 及上半区形式 | 类型转换和宽累加；`SSHLL/USHLL` 已支持，不能把全部 widening 算成缺失 |
| NEON 水平归约 | `ADDV/SADDLV/UADDLV/SMAXV/SMINV/UMAXV/UMINV`、`SADDLP/UADDLP/SADALP/UADALP` | 求和、搜索和归约；`ADDP`、整数 `SMAXP/SMINP/UMAXP/UMINP` 已支持 |
| NEON 最大最小/零比较 | 逐 lane `SMAX/SMIN/UMAX/UMIN`；`CMEQ/CMGE/CMGT/CMLE/CMLT ...,#0` 形式 | 基础向量化；寄存器对寄存器的部分比较已支持 |
| NEON 差值/平均 | `SABD/UABD/SABA/UABA/SABDL/UABDL/SABAL/UABAL`、`SHADD/UHADD/SHSUB/UHSUB/SRHADD/URHADD` | 图像、音频、压缩等 SIMD 算法 |
| NEON 饱和算术 | `SQADD/UQADD/SQSUB/UQSUB/SUQADD/USQADD/SQABS/SQNEG`、`SQDMULH/SQRDMULH/SQDMULL/SQDMLAL/SQDMLSL` | 定点数据和媒体计算；相关饱和/QC 语义尚不完整 |
| NEON 饱和缩窄 | `SQXTN/UQXTN/SQXTUN`，含 `2` 和合法标量形式 | 定点转换；普通 `XTN/XTN2` 已支持 |
| NEON 高位缩窄算术 | `ADDHN/RADDHN/SUBHN/RSUBHN` 及上半区形式 | 加减后截取高位和舍入转换 |
| NEON 剩余移位 | `SRSHL/URSHL/SQSHL/UQSHL/SQRSHL/UQRSHL/SQSHLU`；`SHRN/RSHRN/SQSHRN/UQSHRN/SQRSHRN/UQRSHRN/SQSHRUN/SQRSHRUN`，含上半区；`SHLL/SHLL2` | 舍入、饱和和缩窄移位；向量 `SSHL/USHL`、普通 immediate shift/accumulate/insert 已有子集 |
| 标量 AdvSIMD 形式 | D 寄存器 `SSRA/USRA/SRSHR/URSHR/SRSRA/URSRA/SRI/SSHL/USHL` 等大量形式；标量 `SLI` 语义错误 | 不能根据向量形式已实现就声称同助记符的标量形式也支持 |
| 多项式基础运算 | `PMUL`、字节到半字的 `PMULL/PMULL2` | 部分校验和/GF 算法；64 位 PMULL 的可选扩展另列 |
| 浮点估算/修正 | `FRECPE/FRECPS/FRSQRTE/FRSQRTS/FRECPX`；`URECPE/URSQRTE` | 倒数、平方根估算和优化数值代码；普通 `FDIV/FSQRT` 已支持 |
| 剩余向量浮点 | `FMULX/FABD`；按 lane 的 `FMUL/FMLA/FMLS`；与零比较的 `FCMEQ/FCMGE/FCMGT/FCMLE/FCMLT` | SIMD 数值代码和编译器生成的优化路径 |
| 向量浮点归约 | `FMAXP/FMINP/FMAXNMP/FMINNMP` 的向量/标量 pairwise 形式；`FMAXV/FMINV/FMAXNMV/FMINNMV` | 最大最小归约；`FADDP` 已支持 |
| 向量浮点转换 | `FCVTL/FCVTN/FCVTXN`；向量 `FCVTNS/NU/AS/AU/PS/PU/MS/MU`；向量 fixed-point `SCVTF/UCVTF/FCVTZS/FCVTZU ...,#fbits` | S/D 宽窄转换、其他舍入模式和定点转换；标量 GPR 转换、向量普通 `FCVTZS/ZU` 和 `SCVTF/UCVTF` 有实现 |
| 系统寄存器 | `TPIDRRO_EL0/CTR_EL0/CNTVCT_EL0/CNTFRQ_EL0` 等读取尚不支持 | 直接读取计时器/缓存信息的库；`TPIDR_EL0/NZCV/FPCR/FPSR/DCZID_EL0` 已支持 |
| HINT/缓存维护 | `WFE/WFI/SEV/SEVL`，`DC CVAU/IC IVAU` 等；部分可选 HINT 也缺失 | 专用自旋/缓存代码；`NOP/YIELD/DMB/DSB/ISB/CLREX/DC ZVA` 已有行为 |
| 异常指令 | `BRK/HLT` 未提供各自调试/异常语义；只识别 `SVC #0` | 调试和故意陷阱；非零 SVC immediate 也未支持 |

标量 AdvSIMD 和其他同族变体的完整枚举仍应按 ARM 编码空间逐项扩展，
以上表格是代码核对后的指令族清单，CSV 是实际验证的具体编码。

实现入口：[基础解码](../../crates/trames-cpu/src/lib.rs)、
[整数/NEON](../../crates/trames-cpu/src/extended.rs)、
[访存](../../crates/trames-cpu/src/load_store.rs)、
[标量浮点](../../crates/trames-cpu/src/floating.rs)、
[向量浮点](../../crates/trames-cpu/src/vector_float.rs)。

## CPU：可选扩展与语义边界

| 扩展 | 当前缺口 |
| --- | --- |
| CRC32 | `CRC32B/H/W/X`、`CRC32CB/CH/CW/CX` 全部未实现 |
| LSE / LSE128 等原子扩展 | `CAS/CASP/SWP/LDADD/LDCLR/LDEOR/LDSET/LDSMAX/LDSMIN/LDUMAX/LDUMIN` 及 acquire/release、store aliases；没有相应扩展实现 |
| RCpc | `LDAPR` 等更晚引入的 acquire/release 访存形式未实现 |
| 加密 | AES、SHA1/SHA256、64 位 PMULL，后续 SHA512/SHA3/SM3/SM4 等未实现 |
| FP16 / BF16 | H 标量/向量算术和转换，`BFDOT/BFMMLA` 等未实现 |
| DotProd / RDM / I8MM / 复数运算 | `SDOT/UDOT`、`SQRDMLAH/SQRDMLSH`、`SMMLA/UMMLA/USMMLA`、`FCMLA/FCADD` 等未实现 |
| JS conversion / FlagM / RNG | `FJCVTZS`、`CFINV/RMIF/SETF8/SETF16`、`RNDR/RNDRRS` 等未实现 |
| PAC / BTI | 选定 PAC/BTI HINT 编码按兼容空操作处理；没有认证密钥、认证失败或分支目标检查。`PACIA/AUTIA/XPACI/BRAA/RETAA` 等普通扩展编码不支持，部分 HINT 形式也不支持 |
| MTE | `IRG/ADDG/LDG/STG` 等标签指令及标签检查未实现 |
| SVE / SVE2 / SME | 没有 Z/P/ZA 架构状态或这些指令集实现 |

FPCR 目前仅模拟舍入模式、FZ 和 DN；异常 trap enables 等位没有实现。
FPSR 支持已实现 FP 运算的异常标记，但不构成完整浮点异常/饱和机制。
独占访问采用保守 memory revision，执行顺序依赖协作式调度；已有线程支持
不等于实现了完整弱内存模型或真实多核执行。

CPU fault 当前也不是完整的可恢复 guest 异常：会话主进程返回 runtime 错误，
其他任务直接以 SIGILL/SIGSEGV 状态退出，没有先调用 guest 的同步异常处理器。
这与显式 kill/timer 等异步信号已有的 handler/frame 路径不同。

初始 auxv **没有提供 `AT_HWCAP/AT_HWCAP2`**。在缺少能力声明和语义支持时，
不能仅因 iPad 硬件支持就向 guest 宣布上述扩展。基础 NEON 仍可能由普通
编译器直接生成，不依赖可选扩展探测，因此其缺口会影响通用二进制。

C++ 后端只直接执行选定整数/分支块；其余指令回到 Rust。
它不是第二套更完整的 ISA，切换后端不会补齐兼容性。内存、FP/SIMD 和系统调用
的 C++ 快路径缺失属于性能范围，与上面的 guest 功能缺失分开记录。

## Linux：完全缺失的主要调用族

下表汇总没有实现的调用族；**逐个名称和编号以 CSV 为准**。
除 `socketpair` 外，无入口的调用返回 `ENOSYS`。

| 类别 | 缺失调用/能力 | 对实际软件的影响 |
| --- | --- | --- |
| 事件循环与可轮询通知 | `epoll_create1/epoll_ctl/epoll_pwait/epoll_pwait2`、`eventfd2`、`signalfd4`、`timerfd_create/settime/gettime` | Python asyncio、libuv、事件驱动服务通常需要后端选择或降级；已有 `ppoll/pselect6` 不等于 epoll |
| 本地 socket IPC | `socketpair` 明确返回 `EAFNOSUPPORT`；`socket(AF_UNIX,...)` 不支持；没有 Unix stream/datagram、抽象地址、`SCM_RIGHTS/SCM_CREDENTIALS` | Python socketpair、部分 multiprocessing/asyncio 路径、ssh-agent、本地服务通信 |
| 文件监听 | `inotify_init1/add_watch/rm_watch`、`fanotify_init/mark` | 文件监控、自动重载、编辑器/开发服务 |
| 扩展属性与权限能力 | 全部 `set/get/list/remove xattr` 及 l/f 形式，`capget/capset` | xattr/ACL/文件 capabilities；不能把已有 chmod/chown 当成这些功能 |
| 现代文件接口 | `statx/openat2/close_range/fchmodat2`、`execveat`、`name_to_handle_at/open_by_handle_at` | 新版 libc/工具的接口探测；许多程序有降级路径，需实际验证 |
| 文件空间和同步 | `fallocate`、`sync(81)`、`syncfs/sync_file_range`、`readahead/fadvise64` | 预分配、同步和访问建议；`fsync/fdatasync` 已有实现；81 是上面的错配问题 |
| 文件批量/复制 | `preadv/pwritev/preadv2/pwritev2`、`sendfile/splice/vmsplice/tee/copy_file_range` | 高效复制和高级 vectored I/O；已有普通 read/write/readv/writev/pread/pwrite |
| 共享与管理映射 | `mremap/msync/madvise/mincore/remap_file_pages`、`memfd_create/memfd_secret`、`mlock/munlock/mlockall/munlockall/mlock2` | Python mmap、共享内存、分配器；`MAP_SHARED` 也不支持，见部分实现表 |
| 线程同步补充 | `set_robust_list/get_robust_list`、`futex_waitv/futex_wake/futex_wait/futex_requeue` 新接口、`membarrier/rseq` | robust mutex、较新的线程/运行库优化；旧 futex 有真实等待队列子集 |
| 信号与等待 | `sigaltstack`、`rt_sigqueueinfo/rt_tgsigqueueinfo`、`waitid/restart_syscall` | 备用信号栈、带值实时信号、部分崩溃处理和等待 API |
| 进程新接口/调试 | `clone3`、`pidfd_open/send_signal/getfd`、`ptrace/process_vm_readv/process_vm_writev/kcmp/process_mrelease` | 新版进程管理、调试器、跨进程访问 |
| 资源统计和调度 | `getrusage/times/getcpu`、`sched_getaffinity/setaffinity`、除 yield 外的多数 `sched_*`、`getpriority/setpriority/ioprio_get/ioprio_set` | CPU 数量、亲和性、线程策略、编译工具统计；协作式执行不能直接转发 host 调度 |
| SysV/POSIX IPC | 全部 `shm*`、`sem*`、`msg*`、`mq_*` | System V 共享内存/信号量/消息队列及 POSIX 消息队列；普通 pipe 已支持 |
| 批量网络 | `recvmmsg/sendmmsg` | 批量 UDP/网络服务；TCP/UDP 的基本收发已实现 |
| 挂载/命名空间 | `mount/umount2/pivot_root/chroot/unshare/setns`；`open_tree/move_mount/fsopen/fsconfig/fsmount/fspick/mount_setattr/statmount/listmount` | mount 命令可以列出内置挂载，但创建/卸载挂载仍缺失；没有容器 namespace 语义 |
| 异步 I/O | 旧 AIO 的 `io_*`，包括 `io_pgetevents`；`io_uring_setup/enter/register` | 使用这些 API 的数据库/服务器；是否有同步降级要逐个验证 |
| 内核/系统管理 | `bpf/perf_event_open/seccomp/userfaultfd`、keys、NUMA/mempolicy、swap、模块/kexec/reboot、quota、LSM/Landlock、系统时钟/主机名控制等 | 需要独立虚拟语义或明确产品边界，不能将 guest 参数直接传给 iPad/host |

`lookup_dcookie(18)` 和 `nfsservctl(42)` 在选定 Linux 表中本来就是
`sys_ni_syscall`，不应当作优先补齐的现代 Linux 功能。

## Linux：已有入口但语义不完整

| 接口 | 当前支持 | 剩余限制 |
| --- | --- | --- |
| `fcntl` | dup、FD_CLOEXEC、GET/SETFL | 没有 `F_GETLK/F_SETLK/F_SETLKW`、OFD 区域锁、owner/signal/seals 等；`flock` 的整文件锁是另一套已有实现 |
| `ioctl` / PTY | 一个终端的 termios、窗口、前台进程组、部分网络查询 | 没有多 PTY 分配，`/dev/ptmx` 打开报 `EOPNOTSUPP`；非零 VTIME 被拒绝；通用设备 ioctl 未实现 |
| `openat` / at-family | 普通文件、目录、软/硬链接、权限和主要 flags | 没有 `O_PATH/O_TMPFILE/O_DIRECT` 等完整 flags；`linkat` 未完成全部 flags 语义，不能从入口存在推断 `AT_SYMLINK_FOLLOW`/`AT_EMPTY_PATH` 都正确 |
| `renameat2` | 普通替换和 `RENAME_NOREPLACE` | 没有 `RENAME_EXCHANGE/RENAME_WHITEOUT` |
| `faccessat2` / `fchmodat` | 主要模式和部分 at flags | `faccessat2` 没有 `AT_EMPTY_PATH`；旧 fchmodat 没有完整 flag 扩展，`fchmodat2` 本身缺失 |
| `mmap` | 匿名/文件 `MAP_PRIVATE`、FIXED/NOREPLACE、保护位 | 不支持 `MAP_SHARED`；文件内容预先复制，没有完整 page-cache 一致性、截断后的 SIGBUS、hugepage 等语义 |
| `clone` | fork/COW、共享 VM 的 musl pthread、选定 files/fs/sighand/TLS/TID flags、vfork | 其余 flags/namespace/pidfd 不支持；协作式调度和任务/内存限额仍在；`clone3` 缺失 |
| `futex` | WAIT/WAKE、WAIT/WAKE_BITSET、REQUEUE/CMP_REQUEUE，clear_child_tid 唤醒 | PI、robust owner-died 恢复、WAKE_OP、PI requeue 等未实现；跨独立进程的 shared-file futex 语义不完整 |
| `rt_sigaction` 等 | 常规信号 mask/frame/return、kill/tkill/tgkill、timed wait、部分 restart | `SA_ONSTACK` 拒绝；备用栈缺失；实时信号没有完整多实例排队；CPU 同步 fault 没有交给 guest handler 恢复 |
| timers / clocks | realtime/monotonic POSIX timer、ITIMER_REAL、常用时钟查询 | CPU-time timer/clock 缺失；ITIMER_VIRTUAL/PROF 不支持；部分时钟用 runtime elapsed 或 host realtime 近似，`clock_getres` 固定报 1ns |
| `wait4` / rusage | 子进程退出/停止/继续及基本选项 | rusage 输出写零，`getrusage/times` 缺失，不能据此做真实 guest CPU/内存统计 |
| `getrlimit/setrlimit/prlimit64` | 查询固定 runtime 限额 | 改变实际未强制执行的限额会拒绝；不支持完整动态限额调整或任意目标进程 |
| `prctl` | `PR_SET_NAME/PR_GET_NAME` | PDEATHSIG、dumpable、no_new_privs、seccomp、tagged address 等其他操作缺失 |
| `socket` | IPv4/IPv6 TCP/UDP，虚拟 ICMP，查询用 NETLINK_ROUTE | AF_UNIX/AF_PACKET 等缺失；netlink 只支持选定查询，无路由修改/多播订阅；外部非 echo raw ICMP 不支持 |
| `setsockopt/getsockopt` | 选定 SOL_SOCKET/TCP/IPv6/ICMP 选项 | `SO_RCVTIMEO/SO_SNDTIMEO` 明确拒绝；linger、许多 keepalive 调参、multicast、其他选项未实现；native/virtual 后端支持集合不完全相同 |
| `sendmsg/recvmsg` | iovec、地址、基本 flags、选定 ICMP hop ancillary | 通用发送 ancillary 拒绝；没有 FD/credentials 传递和完整控制消息集合 |
| stat/proc/sys/dev | 虚拟文件与进程、网络和内置挂载视图 | statx 缺失；没有完整 Linux 设备/文件系统/可写 sysctl 集合，内容和容量描述 runtime，不是 host Linux |

实现入口：[调度/进程/信号](../../crates/trames-core/src/process.rs)、
[futex/timer](../../crates/trames-core/src/process_events.rs)、
[Linux/VFS 分派](../../crates/trames-linux/src/state.rs)、
[网络 ABI](../../crates/trames-linux/src/network.rs)、
[VFS](../../crates/trames-vfs/src/lib.rs)。

## 建议后续顺序

1. 修正已确认的 `sync` 编号和标量 SLI 语义，然后补高频基础 NEON/成对独占指令；
   扩展时按具体形式做 QEMU 差分，不以“助记符能解码”为验收。
2. 优先补通用用户态接口：AF_UNIX/socketpair、epoll/eventfd/timerfd/signalfd、
   区域锁、robust mutex/sigaltstack，验证 Python 的异步和多进程路径。
3. 补 `MAP_SHARED/mremap/madvise/memfd`、statx/close_range 等现代接口、
   资源查询/亲和性；以实际 Git、GCC/Clang、Make/CMake 工作流确定先后。
4. CRC/加密/LSE 等可选扩展按真实程序需求推进，并与 HWCAP 声明一起验收；
   SVE/SME 和内核管理接口单独评估范围。

当前每个独立地址空间 64 MiB、会话聚合 256 MiB、任务 32、guest FD 256 等
预算也可能让大型构建失败；这些是资源限额，不能误记为缺少 syscall。
16 GiB 磁盘预算不增加 guest 可映射内存。已有 Python 验收也不代表 asyncio、
multiprocessing、所有 C 扩展或任意软件包都通过。

上述影响是 API/指令依赖分析，不是声称已经重现每个程序失败。
本次没有新增功能、性能改动、版本号或安装包。
