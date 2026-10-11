# CPU / Linux ABI：0.4.0

本轮按“参考实现正确性优先，再做加速；Linux 逐步完善”的顺序推进。
范围是 AArch64 EL0 用户态；不运行 Linux 内核，不包含 EL1/MMU 或 AArch32。
[验证记录](../validation/reference-0.4.md)保留差分、真实程序及性能证据。
先前 [0.3.0 发布版](compatibility-gaps.md)和[实现前工作区](compatibility-gaps-worktree.md)
审计保留原结果，不能用来判断当前版本。

## 本轮 CPU 实现

| 类别 | 行为 |
| --- | --- |
| 整数 NEON | 位选择/反转/计数、比较、pairwise/reduction、halving/absolute difference、饱和加减/窄化、加宽/高位窄化、普通与 indexed 乘加、rounded/saturating/accumulating/insert shifts、polynomial multiply |
| 浮点 | S/D 标量与向量形式补齐；indexed/FMA、fixed-point conversion、round-to-odd、min/max、成对与跨 lane reduction、ARM 量化 reciprocal/reciprocal-square-root 估计和融合 refinement |
| 半精度 | IEEE binary16 标量/向量/indexed 算术与整数转换、H↔S/D 格式转换；FZ16，及格式转换中的 AHP；穷举半精度位型用于 IEEE/AHP widening 验证 |
| 原子与访存 | CRC32/CRC32C，LSE CAS/CASP/SWP 与 RMW 系列、paired exclusive、LDTR/STTR、LDAPR；写入前检查整段访存，fault 保留寄存器/内存状态 |
| 密码与整数扩展 | AES、SHA1、SHA256、64 位 PMULL；DotProd，RDM，I8MM；含 lane/upper/别名寄存器形式 |
| 状态与系统接口 | FlagM、FJCVTZS、TPIDRRO_EL0、虚拟 CTR/CNTFRQ/CNTVCT、选定 HINT 和 cache maintenance；SVC 的所有 immediate 使用 Linux x8 分派 |

FPSR 的异常标记和饱和 QC 是语义的一部分。输入寄存器先快照，覆盖别名和上半区形式。
估计指令使用 ARM 的量化算法；浮点运算使用整数软件实现及明确舍入。

初始 auxv 提供 `AT_HWCAP/AT_HWCAP2`，`/proc/cpuinfo` 使用同一能力定义：
`fp asimd aes pmull sha1 sha2 crc32 atomics fphp asimdhp asimdrdm jscvt lrcpc asimddp flagm i8mm`。
它描述虚拟 CPU，独立于宿主和后端。未实现的 PAC/SVE/MTE/BF16 等能力不公布。
C++ 仍通过 Rust 回退获得这些语义。

## 剩余 CPU 范围

- BRK/HLT 的专门异常、guest 同步 CPU fault 进入并恢复信号 handler 尚未实现；
  WFI 在本 EL0 实现中拒绝。根任务的 CPU fault 仍会终止当前运行。
- 没有 PAC 认证、BTI 目标检查、MTE 标签状态、SVE/SVE2/SME 的 Z/P/ZA 架构状态。
  已接受的部分 PAC/BTI HINT 是兼容空操作，不能当作对应功能支持。
- BF16、FP16 FHM、complex FP、SHA512/SHA3/SM3/SM4、RNG、FRINT32/64、
  RCpc2/LOR/LS64 等后续可选扩展尚未实现；不公布相应 HWCAP。
- FP trap enables 为 RAZ/WI；不提供浮点异常 trap。协作式调度和保守 reservation
  不是真实多核的完整弱内存模型。计数器以进程内共用单调时钟模拟，非宿主硬件计数器。

代表编码清单刻意集中在旧缺口，不是 ARM 全部合法编码的穷举。
接受编码、通过已测输入，与证明完整 ISA 覆盖是不同的证据。

[逐编码结果](../validation/compatibility-0.4-cpu.csv)中 418 条代表编码，
两种后端均为 384 条解码、2 条 SVC、32 条拒绝；恢复实现前同一组有 116 条拒绝。
沿用最初的 317 条样本时，拒绝数由发布版的 256 条降至 22 条。
[元数据](../validation/compatibility-0.4.json)记录源码指纹和探测方式。

## Linux 与默认环境

默认 root 用户的工作目录、HOME 为 `/root`，USER/LOGNAME 为 root。
虚拟网络接口为 `lo`、`eth0`。持久化根目录使用 `/dev/vda1`，stat、mountinfo、
`/proc/partitions` 和 sysfs block/class/dev 别名中的设备号统一为 253:1；vda 为 253:0。
磁盘大小来自逻辑预算，不暴露宿主设备。存储仍是私有 inode 对象和 v3 manifest，
不是 ext4 镜像；原始 block I/O 返回 EOPNOTSUPP。`sync` 修正为 arm64 编号 81，
162 恢复为尚未实现的 setdomainname。

Linux v6.12 的 318 个编号清单仍为 123 项功能子集、192 项缺少实现入口、
1 项明确不支持、2 项上游废弃接口。这个编号集合含配置/架构相关接口，
不是“必须全部完成”的产品清单。无入口返回 ENOSYS，不伪造成功。
[逐编号清单](../validation/compatibility-0.4-syscalls.csv)与元数据保留了
194 个无分派入口编号及 sync/socketpair 在两种后端的返回值探测。

下一阶段 Linux 重点仍为 AF_UNIX/socketpair、epoll/eventfd/timerfd、poll/信号边界、
更多文件操作和同步 fault 恢复，随后以实际 Git/GCC/Clang 等程序验证 M6。
完整 Linux ABI、任意软件包兼容性和真实 iPad 性能尚未证明。
