# CPU / Linux ABI 剩余缺口（实现前的历史工作区快照）

此页保留恢复实现前的审计结果。后续代码已改变，当前能力和剩余范围见
[0.8.0 CPU/Linux 剩余缺口](compatibility-current.md)。

审计日期：2026-10-11。基线提交：
[`4631b0a1`](https://github.com/kb5000/trames/tree/4631b0a1ff0e9f092ddd36b4a1a78eabb7647489)，
加上当前**尚未提交、尚未发布**的实现。不能将这些工作区结果视为 iPad 安装版的能力。
本轮仅审计和记录，没有修改运行时代码、优化性能或生成 IPA。

已发布的 0.3.0 缺口保留在 [发布版审计](compatibility-gaps.md)。
这份清单补充当前实现状态，避免把已有但尚未发布的功能继续列为完全缺失。

## 范围与证据

- CPU 以 AArch64 EL0 用户态为范围，区分基础指令、具体变体与可选扩展。
  不将 EL1/MMU、启动真正 Linux 内核或 AArch32 纳入本清单。
- Linux 编号参照仍为 Linux v6.12 的 arm64 原生 ABI；参照来源和校验值见
  [发布版审计元数据](../validation/abi-audit.json)。
  318 个编号包含废弃、依赖内核配置及架构功能的接口，不是必须全部实现的产品清单。
- [CPU 逐编码结果](../validation/abi-audit-worktree-cpu.csv)：418 条合法代表编码，
  Rust/C++ 后端结果一致；301 条成功解码、1 条进入 SVC、116 条不支持。
  沿用原审计的 317 条样本时，当前是 250 条成功解码、1 条 SVC、66 条不支持。
  样本集中在缺口，**这些数字不是 ISA 覆盖率**。
- [Linux 逐编号清单](../validation/abi-audit-worktree-syscalls.csv)：
  **123 项提供功能子集，192 项无实现入口，1 项明确不支持，2 项为上游废弃空接口**。
  对全部 194 个无分派入口编号及 `sync/socketpair` 做了两种后端探测。
- [工作区审计元数据](../validation/abi-audit-worktree.json)记录源码 SHA-256、
  探测范围、返回值和复现汇编。源码随后变化时，应重新核对本快照。

成功解码只证明这个编码被接受，不证明所有输入、宽度、lane、别名、异常和内存
顺序都正确。另运行现有 S/D 浮点差分：14,828 个寄存器/FPSR 快照、618 个
QEMU cortex-a53 程序比较通过；这也不是完整 ISA 或 Linux 一致性验收。

## CPU：仍未实现的基础形式

普通 W/X 整数、常见访存、S/D 标量浮点和大量 NEON 已有实现。
**浮点已经实现**，剩余缺口主要在具体形式和扩展上。

| 类别 | 当前仍缺少的指令或行为 | 核对结果 |
| --- | --- | --- |
| NEON 按 lane 的加宽乘法 | `SMULL/UMULL/SMLAL/UMLAL/SMLSL/UMLSL` 的 indexed element 形式，含 `2` 上半区形式 | 代表编码全部拒绝；普通向量乘法及普通 long 形式已有实现 |
| NEON 按 lane 的饱和乘法 | `SQDMULL/SQDMLAL/SQDMLSL/SQDMULH/SQRDMULH` 的 indexed element 形式，含合法标量和上半区形式 | 已探测的向量、标量 indexed 形式均拒绝；非 indexed 形式可解码 |
| 标量 round-to-odd 转换 | `FCVTXN S,D` | 拒绝；向量 `FCVTXN/FCVTXN2` 已有实现，不能据此推断标量形式支持 |
| 基础半精度格式转换 | H 与 S/D 的 `FCVT`，半精度与单精度的 `FCVTL/FCVTN` 及 `2` 形式 | 拒绝；这些格式转换与可选 FP16 算术扩展应分开记录 |
| 用户态系统寄存器读取 | `TPIDRRO_EL0/CTR_EL0/CNTVCT_EL0/CNTFRQ_EL0` | 拒绝；已有 `TPIDR_EL0/NZCV/FPCR/FPSR/DCZID_EL0` |
| HINT 与缓存维护 | `WFE/WFI/SEV/SEVL`、`DC CVAU/IC IVAU`；探测中的 `ESB/CSDB` 等扩展 HINT | 拒绝或未提供对应语义；`NOP/YIELD/DMB/DSB/ISB/CLREX/DC ZVA` 已有行为 |
| 调试/异常 | `BRK/HLT` 的各自异常语义；非零 immediate 的 `SVC` | 当前按不支持报错；只识别 `SVC #0` |

这是经源码核对和代表编码探测的缺口，不是 ARM 全部合法编码的穷举结果。
尚需对其余同族形式、保留编码、别名寄存器和 fault 前后状态做系统性验收。

实现入口：[整数/NEON](../../crates/trames-cpu/src/vector_integer.rs)、
[原有整数扩展](../../crates/trames-cpu/src/extended.rs)、
[浮点补充](../../crates/trames-cpu/src/vector_extra_float.rs)、
[访存](../../crates/trames-cpu/src/load_store.rs)。

## CPU：可选扩展与尚未完成的实现

| 扩展 | 当前状态及缺口 |
| --- | --- |
| CRC32 | B/H/W/X 的 CRC32/CRC32C 代表编码已接受，不再列为完全缺失 |
| LSE | `CAS/CASP/SWP/LDADD/LDCLR/LDEOR/LDSET/LDSMAX/LDSMIN/LDUMAX/LDUMIN` 的代表编码已接受；不代表后续 LSE128 等扩展齐全 |
| RCpc / LOR / LS64 | **`LDAPR` 有代码但实际无法执行**，见下文；`LDAPUR/STLUR/LDLAR/STLLR/LD64B/ST64B` 代表编码也拒绝 |
| AES / SHA 等加密 | AES、SHA1/SHA256 未实现；后续 SHA512/SHA3/SM3/SM4 等也没有实现。64 位 `PMULL/PMULL2` 已有代码并接受代表编码 |
| FP16 / BF16 | H 标量/向量算术、H 与 GPR 的 FMOV/整数转换，以及 `BFDOT/BFMMLA` 等仍缺失 |
| DotProd / RDM / I8MM / 复数运算 | `SDOT/UDOT`、`SQRDMLAH/SQRDMLSH`、`SMMLA/UMMLA/USMMLA`、`FCMLA/FCADD` 等仍缺失 |
| JS conversion / FlagM / RNG / 新舍入形式 | `FJCVTZS`、`CFINV/RMIF/SETF8/SETF16`、`RNDR/RNDRRS`、`FRINT32*/FRINT64*` 等仍缺失 |
| PAC / BTI / MTE | 选定 PAC/BTI HINT 是兼容空操作，没有认证或目标检查；普通 PAC 编码、MTE 标签指令和标签检查仍缺失 |
| SVE / SVE2 / SME | 没有 Z/P/ZA 架构状态或完整指令实现 |

`LDAPR` 的确认问题：`LDAPR X0,[X1]` 的合法编码 `0xf8bfc020` 被前面的
LSE RMW 掩码捕获，随后按 operation 12 拒绝，没有走到后面的 LDAPR 分支。
数据为 7 的独立程序在 QEMU `-cpu max` 下退出码为 7；两种 Trames 后端
均以 runtime 错误退出（125）。本轮只记录，没有修复。
现有 `atomics_crc.py` 未覆盖 LDAPR，不能用该测试通过来证明 RCpc 已完成。

还存在几项不能靠增加解码入口解决的边界：

- FPCR 仅模拟舍入模式、FZ 和 DN；FP16 控制和浮点异常 trap enables 没有完整实现。
  工作区 FPSR 已有饱和 QC 的更新代码，具体指令仍需差分验收。
- 独占访问与原子操作运行在协作式调度和保守 memory revision 模型下，
  不构成真实多核的完整弱内存模型。
- 初始 auxv 没有 `AT_HWCAP/AT_HWCAP2`；未来应与实际能力和语义验收一起补齐。
- CPU 同步 fault 没有先交给 guest 信号 handler 恢复：主进程返回 runtime 错误，
  其他任务直接以 SIGILL/SIGSEGV 状态退出。
- C++ 后端的许多指令回退到 Rust；切换后端不会自动补齐 ISA。

## Linux：完全缺失的主要调用族

以下按程序依赖归类；完整名称、编号、状态和已有入口位置以 CSV 为准。
无入口返回 `ENOSYS`，`socketpair` 明确返回 `EAFNOSUPPORT`。

| 类别 | 主要缺失调用/能力 | 常见影响 |
| --- | --- | --- |
| 事件循环和通知 | `epoll_create1/ctl/pwait/pwait2`、`eventfd2/signalfd4`、`timerfd_create/settime/gettime` | asyncio、libuv、服务端事件循环 |
| 本地 socket IPC | `AF_UNIX`、`socketpair`、抽象地址、FD/凭证传递 | Python 部分 multiprocessing/asyncio 路径、ssh-agent、本地服务 |
| 文件监听/权限扩展 | `inotify_*`、`fanotify_*`、全部 xattr 家族、`capget/capset` | 自动重载、文件监听、ACL/capabilities |
| 现代文件和设备接口 | `statx/openat2/close_range/fchmodat2/execveat`、`mknodat`、文件 handle 接口 | 新版 libc/工具探测、创建设备节点 |
| 文件空间、复制和同步 | `fallocate/syncfs/sync_file_range/readahead/fadvise64`；`preadv/pwritev` 及 v2、`sendfile/splice/vmsplice/tee/copy_file_range` | 预分配、复制、批量 I/O；普通 read/write、fsync/fdatasync 已有 |
| 映射/共享内存 | `mremap/msync/madvise/mincore/remap_file_pages`、`memfd_create/memfd_secret`、`mlock*`/`munlock*`；已有 mmap 缺 `MAP_SHARED` | Python mmap、共享内存、分配器、数据库 |
| 线程同步和信号 | `set_robust_list/get_robust_list`、新 futex 家族、`membarrier/rseq`；`sigaltstack/rt_sigqueueinfo/rt_tgsigqueueinfo/restart_syscall` | robust mutex、备用信号栈、较新运行库 |
| 进程、等待和调试 | `clone3/waitid`、`pidfd_*`、`ptrace/process_vm_*`、`kcmp/process_mrelease` | 调试器、新进程管理接口 |
| 资源统计和调度 | `getrusage/times/getcpu`、亲和性及多数 `sched_*`、priority/ioprio 家族 | CPU 数量和时间查询、编译器统计、调度策略 |
| IPC 和批量网络 | SysV `shm*/sem*/msg*`、POSIX `mq_*`；`recvmmsg/sendmmsg` | 多进程 IPC、消息队列、批量 UDP |
| 挂载和 namespace | `mount/umount2/pivot_root/chroot/unshare/setns`，现代 mount 家族 | 自定义挂载、隔离和容器；已有内置挂载列表不是 mount 系统调用实现 |
| 异步 I/O | 旧 AIO `io_*`、`io_uring_*` | 部分数据库和服务器 |
| 内核/系统管理 | bpf/perf/seccomp/userfaultfd、keys、NUMA、swap、modules/kexec/reboot、quota、LSM/Landlock、主机名/域名和系统时钟设置等 | 需虚拟语义或明确范围，不直接转发给 iPad/host |

`lookup_dcookie(18)` 和 `nfsservctl(42)` 在参照表中本来就是废弃的空实现，
应排除于优先补齐列表。工作区已经修正 `sync` 分派为编号 81，实测返回 0；
`setdomainname(162)` 现在正确进入未实现路径，返回 `ENOSYS`。
发布版 0.3.0 的编号错配仍见旧审计，不能认为用户现有安装包已修复。

## Linux：已有入口但仍不完整

| 接口 | 剩余限制 |
| --- | --- |
| `fcntl` / 文件锁 | 没有 POSIX/OFD 区域锁 `F_GETLK/F_SETLK/F_SETLKW`、seals、owner/signal 等；已有 `flock` 整文件锁 |
| PTY / `ioctl` | 没有多 PTY 分配；`/dev/ptmx` 和非零 termios VTIME 明确拒绝；缺通用设备 ioctl |
| `openat` / at-family / `renameat2` | 缺完整 `O_PATH/O_TMPFILE/O_DIRECT`，部分 `AT_EMPTY_PATH/AT_SYMLINK_FOLLOW` 语义；缺 `RENAME_EXCHANGE/WHITEOUT` |
| `mmap` | 仅匿名/文件 `MAP_PRIVATE`；文件内容复制到映射，缺共享 page cache、一致性及截断后 SIGBUS 等语义 |
| `clone` / `futex` | 选定 fork/pthread flags 与协作式调度；缺 namespace 等 flags、PI、WAKE_OP、robust owner-died 及跨文件共享 futex 语义 |
| 信号 | 缺 `SA_ONSTACK`、完整实时多实例排队、CPU 同步 fault 的 guest handler 恢复 |
| clocks / timers / rusage | 缺 CPU-time clock/timer 和 ITIMER_VIRTUAL/PROF；部分时间近似；`clock_getres` 固定 1ns；`wait4` rusage 写零 |
| `prctl` / rlimit | 仅 `PR_SET_NAME/GET_NAME`；限额查询为 runtime 固定预算，缺完整动态调整及任意目标进程操作 |
| 网络 ABI | 缺 AF_PACKET、完整 raw ICMP、route netlink 写入/订阅；缺 socket 收发超时、许多 multicast/keepalive 选项及通用 ancillary；native/virtual 集合有差异 |
| proc/sys/dev 与 Files | 只有 runtime 的虚拟视图和选定端点；非完整 Linux 可写 sysctl 或设备系统；Files 为授权导入/导出，不是共享文件系统 live mount |
| 块设备 | 工作区 `vda/vda1` 目前只有容量和设备身份视图；原始块读写返回 `EOPNOTSUPP`，没有 ext4 镜像、格式化或真正块层挂载；diskstats 不统计真实块 I/O |

实施 Linux 子集时需要同时记录 flags、操作号和错误行为；不能只把 syscall 名称
从“缺失”移动到“已实现”。普通文件和目录的写入/权限支持，不等于完整 ACL、
xattr、设备节点和文件系统语义；也不意味着 `/usr` 整体只读。

## 后续优先级建议

1. 正确性和基础 CPU：先处理 LDAPR 解码遮挡，补 indexed long/saturating multiply、
   标量 FCVTXN、基础 H 格式转换；继续验证已接受的具体变体和异常状态。
2. 通用 Linux 用户态：AF_UNIX/socketpair、epoll/eventfd/timerfd/signalfd、
   区域锁、robust mutex、sigaltstack；以真实 Python 和开发工具工作流验收。
3. 共享映射与现代接口：MAP_SHARED/mremap/madvise/memfd、statx/openat2/close_range、
   资源统计和亲和性；按 Git、编译器和构建工具的实际失败点推进。
4. AES/SHA、更多可选 ISA 与能力声明按实际二进制需要增加；SVE/SME、
   容器/内核管理接口单独确定范围。性能优化另行进行。

当前独立地址空间 64 MiB、会话聚合 256 MiB、任务 32、guest FD 256 等仍是资源预算。
16 GiB 磁盘预算不会增加可映射内存。遇到 ENOMEM 或任务/FD 上限，应与未实现的
指令、系统调用和语义错误分开诊断。上面的程序影响是依赖分析，不代表每个程序
失败都已重现。
