# 选择性惰性标志与解释器短路径

2026-10-16，基线 `09ae2c5340410bdc50ed881706c036506a2c70eb`。
仍是同一个非 JIT 解释器；所有程序共用这些路径，没有 CoreMark 专用逻辑。

## 最终实现

- ADD/SUB、CMP/CMN 和 CCMP/CCMN 的算术分支保存操作数，按需计算 C/V。
  N/Z 保留为立即计算的简单结果判断，让常用 EQ/NE/MI/PL 不必检查延迟状态类型。
  减法的有符号条件直接比较原操作数，不先计算溢出再组合 N/V。
- 解释循环携带局部标志状态，在退出和进入完整指令语义路径时同步给 Cpu；
  回退返回时读取新的状态。这样普通指令不必反复通过整个 CPU 对象读写标志。
- 不更新标志的立即数 ADD/SUB 使用单独的解码操作，SUB 在解码时折成负偏移。
  执行时只需 wrapping add 和目的位宽截断，保留 SP/WSP 语义。
- NZCV 状态改为私有；`flags()`/`nzcv()` 总是返回完整值，显式写入会丢弃旧的
  延迟状态。ADC/SBC、RMIF、CFINV、SETF8/16、FlagM2、FP 条件比较以及信号
  保存/恢复均经过统一接口。延迟状态随 CPU clone/线程迁移保存，不增加锁或 unsafe。
- 设备 IPA 的 Swift 构建从 Xcode Debug 改为 Release，CI 的打包、产物路径一起更新。
  Rust 核心原本就是 Release + Thin LTO，继续保持。模拟器测试仍用 Swift Debug。
  本批不修改已发布 IPA、版本号或固定旧源码的 0.15 发布验证脚本。

## 前后对照

同一台共享 x86_64 Xeon 开发机，旧/新 release 二进制交替运行，各三次取中位数。
单线程工作负载固定在同一逻辑 CPU 0；四线程测试固定允许 CPU 0–3。
没有本任务的编译、其他测试或并行跑分干扰，但仍不能控制宿主频率和其他租户。

| 工作负载 | 基线秒 | 新版秒 | 基线耗时 / 新版耗时 |
| --- | ---: | ---: | ---: |
| CoreMark，默认单线程 -O2，5000 次迭代 | 17.124 | 15.165 | 1.129× |
| 普通标量/向量访存循环 | 0.23185 | 0.22498 | 1.031× |
| 四线程计算 / 4 vCPU | 0.38146 | 0.33387 | 1.143× |
| Vim 默认配置首次光标 | 1.84984 | 1.83552 | 1.008× |
| Python 导入 / SQLite / hash 工作流 | 8.39169 | 7.92185 | 1.059× |
| GCC 编译、链接并执行 | 2.65437 | 2.48840 | 1.067× |

Vim 的差异很小，不据此认定提速。其余也是本轮观察值，不是置信区间或承诺。
这些数字是最终组合的效果，不能单独归因于惰性标志。新旧使用相同 guest 二进制、
原版签名包、完整 hybrid musl、配置与新复制的 store；CoreMark 校验全部通过，
每次计分超过 10 秒。没有修改 guest 时钟。

ARM64 runner 在 QEMU 下的普通访存三次中位数为 **3.36756 → 3.15029 秒，1.069×**。
它验证另一套宿主指令生成的运行情况，不能代表真实 ARM64 的分支预测、缓存和耗时。
本轮没有物理 iPad 跑分，也没有将旧原生分数与本轮分数混算效率。

[JSON 证据](lazy-flags.json)包含正式样本的完整输出、计数器与二进制/输入指纹，
以及所有中间方案的样本耗时、输出和指纹。

## 为什么没有全部延迟

先实现了全部 NZCV 延迟的三值版本，又试了只保留两个操作数的版本。
它们虽然通过正确性测试，但每次写入更多状态、读取常用条件时增加状态分支，
没有带来稳定的整体收益，普通访存循环反而退化。两值版本的 CoreMark 约持平。
随后保留 N/Z 的直接读取，延迟 C/V，并缩短非 S 立即数路径；最后把标志状态
移出循环中的 CPU 对象访问路径。正式对比才采用这个组合。

中间版本未固定 CPU，部分样本还出现明显宿主波动；不跨轮比较绝对秒数，不把
不同轮次的中位数当作单项修改收益。未采用版本仍保留测量结果，不选择性删除慢样本。
完全惰性并不自动意味着更快；更大幅度提速仍需继续处理指令派发、寄存器和基本块执行。

## 正确性与复现

- 全仓库全部 feature：344 项 Rust 测试；CPU 的 40 项测试也在 ARM64/QEMU 上通过。
- 新增 22,272 组独立 AArch64/QEMU 对照，覆盖 32/64 位加减、全部条件、进位链、
  CCMP/FCCMP、FlagM/FlagM2、显式覆盖及完整 GPR/NZCV 快照。
  x86_64 和 ARM64 runner 各执行一遍，全部匹配。
- 实际 musl 信号处理验证：在带延迟标志的算术之后触发 BRK 和访存故障，检查
  ucontext 的 PSTATE，再修改 NZCV，通过 rt_sigreturn 恢复并读取验证。
  两种宿主构建均通过。另通过基础 71 组 / M1 67 组 QEMU 对照和 3 个故障检查。
- 1/2/4 vCPU 的 pthread、原子/屏障、fork/exec、共享 futex 与原生 Linux/QEMU
  对照通过；19 个并行原生入口、嵌套回调、共享栈及内存/磁盘配置回归通过。
- 严格全仓库 Clippy、fmt、iOS ARM64 core/distro/FFI 编译通过。
  Linux 环境不具备 Xcode，设备 Release 链接和模拟器验收交给 Apple CI。

复用 [execution.py](../../tests/performance/execution.py)，旧/新二进制均由
`cargo build --release -p trames-cli --locked` 生成；本地工具链为 Rust 1.90.0。
在拥有对应逻辑 CPU 的 Linux 主机上：

```sh
taskset -c 0 python3 tests/performance/execution.py \
  --baseline /path/to/baseline --candidate target/release/trames-run \
  --baseline-commit 09ae2c5340410bdc50ed881706c036506a2c70eb \
  --store /path/to/prepared-store --coremark-iterations 5000 \
  --workloads coremark cpu-memory vim-default python-imports gcc-build-run \
  --output target/lazy-flags/host.json
```

四线程工作负载单独使用 `taskset -c 0-3` 和 `--workloads smp-work`。
ARM64/QEMU 对照另加 `--emulated-host`，不能作为设备性能数据。
测试无性能阈值；新增 NZCV 差分已接入 Linux CI。
