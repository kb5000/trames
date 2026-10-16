# CoreMark：宿主原生对照和解释器热点

2026-10-16，运行时代码 `2d078fd45e93398250e53edcc5d53340ce14444b`。
本次只增加手动性能工具和记录，没有修改运行时。完整输出、校验和、编译参数和
采样分类见 [JSON](coremark-profile.json)。

## 同一台机器上的分数与吞吐效率

| 运行方式 | 三次分数范围 | 中位数 |
| --- | --- | --- |
| 宿主原生 x86_64 | 18,313–22,452 | 19,757 |
| Trames，AArch64 guest，1 vCPU | 248–272 | 264 |

另外，保持默认单线程程序不变、仅给 guest 配置 4 vCPU，单次得分 266。
默认 `make PORT_DIR=linux` 的 `MULTITHREAD=1`，增大 vCPU 数不会自动并行化这个程序。

按中位数，**本机 CoreMark 吞吐效率 = 263.829 / 19756.834 ≈ 1.34%**，
即相同迭代工作量约慢 75 倍。样本极值对应约 1.1%–1.5%，这是观察范围，
不是置信区间。宿主是共享 Linux Xeon Platinum 8573C，5 个逻辑 CPU，
cgroup CPU 配额为 4 核；未固定频率/核亲和，原生与 guest 分批运行。

这是相同源码与默认 `-O2` 下的端到端吞吐比较。guest 由 Alpine GCC 14.2 编译
为 AArch64，原生由 Debian GCC 14.2 编译为 x86_64，包含 ISA、编译目标、GCC
配置/补丁差异；**不能直接当成 iPad 的原生效率，也不是指令数或周期的比值**。
iPad 得分 370 与当前解释器量级相符；准确的 iPad 效率需要同一台 iPad 的原生分数。

## 输入与计时

固定官方 EEMBC CoreMark 提交
`1f483d5b8316753a742cbf5590caf5bd0a4e4777`，未修改算法源码。
在新 Alpine 3.22.6 aarch64 guest 内安装原版签名 GCC 14.2.0-r6、
musl-dev 1.2.5-r12、Make 4.4.1-r3，执行 `make PORT_DIR=linux compile`。
与用户命令生成同样的默认动态链接程序，flags 为 `-O2 -lrt`，没有额外 LTO/PGO
或 march=native。宿主使用同一份源码和默认 Linux port。

性能 seeds 为 `0 0 0x66`，总数据量 2000 字节，执行三个算法；固定 3000 次
迭代，三个计分区间分别为 11.028、11.371、12.086 秒。另跑 validation seeds
`0x3415 0x3415 0x66`，11.883 秒、252.46 分。宿主三次计分也均超过 10 秒，
所有运行都输出 `Correct operation validated`。最终 CRC 随迭代次数变化，
算法的参考 CRC 验证均通过。

基线使用优化 release runner；诊断使用 `CARGO_PROFILE_RELEASE_DEBUG=1` 的
优化 release runner。两者源代码相同，后者只为地址符号化保留调试信息。
不使用 debug 优化级别，不修改 guest 时钟；编译和包安装均在计分前完成。
初次交叉编译预跑与后续编译任务有重叠，没有计入上面的分数。

## 时间花在哪里

中位数运行退休约 8.90 亿条 guest 指令，约 **7700 万条/秒**；
每次 CoreMark 迭代约 29.66 万条（含少量进程启动工作）。若编译结果相近，
370 分相当于约 1.10 亿条 guest 指令/秒，作为量级换算即可。

- 约 95% 运行时间在 CPU slice 内；系统调用累计约 0.00012 秒，整个程序只有
  12 次系统调用，idle 为 0。CoreMark 的瓶颈落在 CPU 执行路径。
- 约 113 万次代码页查找、38 次 miss，只新解码约 6470 条指令。
  解码缓存命中率约 99.997%，热循环没有反复完整解码。
- `context_switches` 包含本地 4096 指令量子的返回计数，不能理解成同等数量的
  宿主线程上下文切换。NativeCall 基线关闭；按 CoreMark 源码，链表排序、矩阵、
  状态机和 CRC 位于程序自身，扩展 musl 加速函数难以覆盖主要计时工作。

对优化 runner 做宿主 PC 采样，取得 3163 个有效样本：

| 按源码栈归类的独占样本 | 占比 |
| --- | --- |
| 指令执行、寄存器和条件/标志等辅助函数 | 43.2% |
| 派发、访问已解码记录、代码缓存 | 31.7% |
| guest 访存、页状态、COW 与代码失效状态 | 20.9% |
| 其余，含启动、系统库与内核调度代码 | 4.3% |

这些是 Linux `ITIMER_PROF` 保存的用户态 PC 样本，按内联源码栈分类，
不是硬件周期计数，也不能直接套到 iPad。采样包含进程启动；请求周期 1 ms，
实际频率受宿主 kernel tick 限制。相邻采样/未采样运行约相差 5.3%，同时含
共享机器波动，不把它解释成精确的采样开销。最初一次采集被继承 preload 的
宿主时区辅助进程抢先写出空文件，已丢弃；最终采集同时核对 executable 和 PID。

## 两个具体热点

`cached_slice` 独占样本约 29.5%，其中 cached.rs:641–643 的索引、generation
检查和 `entry.instruction` 字段读取占约 23.6%。宿主反汇编显示热命中路径仍要
读取多个解码字段、向栈保存若干字段，再进入通用执行逻辑。本次 x86_64 构建
的解码 entry 步长为 40 字节，内含 8 字节 generation 与 32 字节 instruction；
原始 AArch64 指令只有 4 字节。命中缓存仍有记录访问、复制和派发成本。
优化后的源码行归因不等于每条 load 的精确延迟，尚未测硬件 cache miss。

另一个明确热点是 `Page::write_bytes` 的 `Arc::make_mut`：即使普通 RAM 页已经
独占，宿主 x86_64 代码仍包含 `lock cmpxchg` 检查引用状态。该函数约 4% 样本，
其余访存成本还包括地址到页的转换、权限/backing 分支、数据访问和状态维护。
这是宿主的 COW 管理工作，不代表 guest 程序使用了原子 store。

一个 guest ADD 的算术步骤可以使用原生宿主加法；完整解释步骤还包括读取解码
记录、读取虚拟寄存器、处理 32/64 位与 guest 标志、写回 PC/寄存器以及派发下一条。
原生程序把寄存器值和更多控制流留在编译后的机器代码中，两个执行成本差距很大。

## 优化顺序

1. 缩小并精简已解码记录的热路径，减少按值取出通用大记录、无关字段搬运和
   重复派发；检查引用形式、紧凑表示与常用指令组合的生成代码。
2. 缩短寄存器、宽度、shift/condition/flags 的通用辅助链，按热点有选择地内联，
   同时控制生成代码大小。
3. 普通独占 RAM 访问尽量复用页转换/权限与 COW 状态，把必要检查移动到映射、
   fork、借用或状态变化边界；保留共享映射、真正 COW、自修改代码和并发可见性。
   优先减少每个普通 store 的 Arc 原子管理及重复状态查询。

这批不引入 JIT，也没有测优化前后加速比。按该份采样，单独把约 21% 的访存
成本全部消除也只有约 1.26 倍的理论收益；这只是 Amdahl 优先级示意，不能作为
设备性能预测。较大提升需要同时缩短派发和指令执行路径。

一个不同工具链配置的 static 交叉构建单次得到 210 分，代码页查找多于 Alpine
默认动态构建；完整诊断单独保留在 JSON。它同时改变工具链配置和链接方式，
不据此归因静态链接成本，也不混入默认构建的分数。

## 复现与后续采样

先保留普通 release runner，再构建带符号的优化版本：

```sh
cargo build --release -p trames-cli
cp target/release/trames-run target/trames-run-baseline
CARGO_PROFILE_RELEASE_DEBUG=1 cargo build --release -p trames-cli
cc -shared -fPIC -O2 -std=c11 -Wall -Wextra -Werror \
  tests/performance/pc-sampler.c -o target/pc-sampler.so -ldl
```

在同一份固定源码下，guest 内执行 `make PORT_DIR=linux compile`；
宿主执行 `make PORT_DIR=linux CC=cc compile`，分别保留 ELF。guest 动态程序
需要原有 Alpine store；静态 ELF 可省略 store。下面的 ELF、store 和 native ELF
路径替换为自己的输入：

```sh
python3 tests/performance/coremark.py \
  --runner target/trames-run-baseline \
  --elf /path/to/guest-coremark.exe --store /path/to/alpine-store \
  --guest-program /root/coremark/coremark.exe \
  --native-elf /path/to/native-coremark.exe \
  --profile-runner target/release/trames-run --sampler target/pc-sampler.so \
  --upstream-commit 1f483d5b8316753a742cbf5590caf5bd0a4e4777 \
  --iterations 5000 --output target/coremark-study/results.json
```

脚本默认只做三次计分、一次 validation、一次单线程/四 vCPU 对照；可选原生
基线和一次带符号对照/采样。每次正式计时必须至少 10 秒，更快的机器应增加
iterations。原始输出保存在相邻的 `results-runs` 目录；再次采样使用新输出目录。
不设置 CI 性能阈值。

iPad 内的 guest `perf` 无法看到宿主 Rust 解释器热点。要测 iPad 上的真实效率，
后续可在 App 内增加明确的原生 CoreMark 对照和运行统计导出；宿主详细函数采样
则需要设备侧诊断支持或 Mac Instruments。当前记录没有 iPad 原生基线或设备采样。
