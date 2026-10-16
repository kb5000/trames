# 默认尾调用解释器与权限软 TLB

2026-10-16。基于 `996ca16b4909eb5883bc2fb2c888e5edfaeedcbf`。
按项目所有者决定，上一轮完整 `tailhot` 实现已转正，替换原来的派发循环。
保持一个生产解释器、原有语义回退和测试 oracle；不引入 JIT 或后端选项。

## 交付

- 工具链及 Linux、Apple CI 固定到 `nightly-2026-10-16`，rustc
  `4ebd258257737d5b7635c116fa7d281627a7e937` / LLVM 23.1.4。
  26 类处理器和冷解码使用编译器保证的 `become` 与 preserve-none ABI。
  生产不使用 RUSTC_BOOTSTRAP；已有 TSan 流程也直接使用该 nightly。
- 继续采用 40 字节解码记录和 31 项 GPR 数组；没有合入上一轮被否决的
  compact/packed/zero 布局。私有寄存器类型与页内索引给出 unchecked 的边界。
- 协调线程及并行 worker 的 256 项软 TLB 仍为 4 KiB，总体积不增加。
  页标签同时编码权限，命中时用一次比较确认页与所需权限；去掉已被有效映射
  证明的单页地址范围检查以及已验证 frame 的数组/Option 重复检查。
- 映射与权限变更清空协调线程 TLB；worker 持有固定映射快照。缓存保存索引，
  不保存容易在 COW/discard 后失效的字节指针。文件 backing 继续实时检查；
  保留跨页失败原子性、脏页提交、代码失效和 ARM 原子/屏障语义。
- 适配新工具链的原子 API 更名和格式要求。三个建议性代码风格 lint 显式允许，
  compute 测试模块顺序局部允许；其他 Clippy 检查仍以 `-D warnings` 验证。

[尾调用设计](../architecture/tail-dispatch.md) ·
[软 TLB 与硬件 MMU 研究](../architecture/soft-tlb.md)。

## 测量

这里测的是 **同一 nightly、相同尾调用实现之上的访存改动**。baseline 是
应用 `tailhot` 并切换工具链后、修改访存之前保存的 release runner；不是
stable 编译的旧循环。最终 candidate 包含工具链适配、权限 TLB 和全部回归修复。
原始 runner、libc、fixture、store 指纹及全部采样保存在 [JSON](tail-tlb.json)。

共享 x86_64 开发机；单线程固定 CPU 0，四线程允许 CPU 0–3。交替运行新旧
程序，各三次取中位数；测量期间没有本任务的构建或测试并行。CoreMark 使用
相同上游默认单线程 `-O2` 程序、5000 次迭代，校验 CRC 且每次计分超过 10 秒。
Vim 保留默认配置，Python/GCC 使用相同 hybrid musl，每次复制新 store。
访存循环是原 fixture 的 1000 万次构建，结果和退休指令数均检查一致。

| 工作负载 | 尾调用基线秒 | 加软 TLB 秒 | 基线 / 新版 |
| --- | ---: | ---: | ---: |
| CoreMark | 14.776000 | 13.778000 | 1.072× |
| 1000 万次访存循环 | 2.467708 | 2.168663 | 1.138× |
| Vim 首次光标 | 2.051892 | 1.972408 | 1.040× |
| Python 导入 / SQLite / hash | 9.028124 | 8.686857 | 1.039× |
| GCC 编译、链接、执行 | 2.781135 | 2.522315 | 1.103× |
| 四线程持续计算（每线程 3000 万次） | 2.269477 | 2.261680 | 1.003× |

最终构建的访存循环约提升 **14%**。四线程原始 300 万次样本仅约 0.2 秒，
中位数为 0.942×；将同一负载延长到每线程 3000 万次复核后为 1.003×，
没有显示出持续并行计算收益或退步。短样本和延长样本均保留。为此测量工具
新增 `--smp-work-iterations`，默认次数不变，不影响运行时或 fixture 算法。


先行测量也全部保留，不能只选择有利样本：访存循环 1.252×，CoreMark
0.989×，Vim 1.024×，Python 0.971×，GCC 1.012×。它与最终构建的指纹不同，
之后补完了 nightly 的代码风格适配，所以最终构建另做完整复核。
两个阶段的时间也显示宿主波动，尤其应用启动和短多核负载。数据不建立物理
ARM64/iPad 提速结论，不把尾调用上一轮的收益与这里的比例直接相乘。

ARM64 release 汇编抽查：`Memory` 的 NOP（336 字节）和 AddImmediate
（404 字节）处理器没有入口保存栈帧；MemoryImmediate 仍建立 128 字节帧，
调用普通访存 helper。JSON 保留入口汇编、地址和符号大小。guest 寄存器数组
读写、指令派发、COW、backing 及并行同步仍有成本。

## 验证

本地最终源码通过：

- `cargo fmt --all --check`、workspace/all-targets/all-features 严格 Clippy。
- workspace 全特性 **351 项**测试。
- ARM64/QEMU CPU **43 项**和 memory **49 项**，合计 **92 项**。
- 两种 host 构建都验证了 256 KiB 宿主线程栈内完成 300 万条 guest 指令，
  包括百万次循环、冷解码、融合分支、准确指令预算和系统调用退出。
- 全标准库 ThreadSanitizer：memory、accel 及新集成测试 **74 项**，无报告。
- 最终 x86 release 对独立 QEMU：基础 71 项比较及 3 个故障检查、M1 67 项；
  22,272 组 NZCV 序列、2,572 个原子/CRC 快照、19,872 个现代指令快照。
- 最终 x86 release 与 ARM64/QEMU release 均完成 22,272 组 NZCV 对照、真实
  musl CPU 故障处理/上下文恢复，以及 1/2/4 核 pthread、LSE/LLSC/CASP、TLS、
  futex、跨页发布、fork COW、exec 和同页独立访问。
- mmap 集成测试在内存与磁盘后端均覆盖共享/私有映射、文件截断 SIGBUS、
  COW/discard、remap、memfd/seals、代码别名失效。
- 原生并行测试覆盖 19 个入口、FP/errno、解析、嵌套排序/搜索回调、共享栈及
  内存/磁盘后端。没有新增全局锁或原生函数串行化。
- iOS ARM64 的 CPU crate **release codegen** 通过；core/distro/ffi iOS 编译
  检查通过。完整 Xcode 链接、模拟器验收由提交后的 Apple CI 执行。

新增回归专门检查：权限组合与热缓存撤权、共享 backing 和私有 COW 后的 EOF、
地址标签/48 位上界、跨页失败不修改前半页，以及 fork/discard/remap/frame 扩容。

提交触发的 Linux/Apple CI 状态见 [Issue #23](https://github.com/kb5000/trames/issues/23)。
本次没有调整版本号或创建新的 Release；设备速度仍待真实 iPad 测量。

复现访存对照（baseline 需在应用 `tailhot` 后、修改访存前保存）：

```sh
# 将原访存循环延长，以减少短样本的计时噪声。
aarch64-linux-gnu-as --defsym iterations=10000000 -o /tmp/cpu-memory-long.o tests/fixtures/cpu-memory.S
aarch64-linux-gnu-ld -static --build-id=none -z noexecstack -o /tmp/cpu-memory-long /tmp/cpu-memory-long.o

taskset -c 0 python3 tests/performance/execution.py \
  --baseline /path/to/tail-baseline --candidate target/release/trames-run \
  --baseline-commit 996ca16+tailhot --store /path/to/identical-app-store \
  --samples 3 --coremark-iterations 5000 --cpu-memory-fixture /tmp/cpu-memory-long \
  --workloads coremark cpu-memory vim-default python-imports gcc-build-run \
  --output /tmp/tail-tlb.json
```
