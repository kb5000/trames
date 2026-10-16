# 解释器调度、边界检查与寄存器实验

> 后续状态：项目所有者已决定将 `tailhot` 转正，当前 main 使用固定 nightly 的
> 尾调用解释器；参见[转正与访存验证](tail-tlb.md)。下文保留本次实验完成时的
> 决策和数据，归档补丁仍只适用于所列历史基线。

2026-10-16。生产基线：`357d80c246fe744333f0e0f486a1d78cc50f3616`。
这是实际 guest 程序的对照实验，不是旧的 C++ 整数调度微基准。
生产仍使用 Rust 1.90.0 的单一缓存解释器；没有 JIT，也没有恢复多后端选择。

## 收敛结果

默认 CPU 执行实现保持基线不变。固定长度代码页确实能让 LLVM 删除重复的
slice 长度加载和越界分支，但独立复核仍出现明显的计算负载退步，因此也撤回。
寄存器布局、代码记录、代际和工具链均没有因这轮实验而替换。

nightly 的完整尾调用原型取得了计算负载收益，作为可复现补丁保留，供后续
ARM64 真机评估。两种 32 字节记录节省了 20% 指令记录内存，但完整复核中
出现退步。单独改变零寄存器布局、局部 PC、冷路径或 unchecked 索引的收益
也不够均衡，没有将这些实验设为默认后端。

试过的 `assert_unchecked` 仅用于由私有五位寄存器构造器保证的范围。所有
候选均保留 guest 地址转换、权限、跨页、文件末尾和原子访问语义。
最终提交增加可复现实验、延长访存测量的工具和寄存器边界回归测试。

最终回归测试放在独立的 `tests/register_state.rs` 中。CPU 运行时源码与基线
没有差异；本地重建的 x86_64、ARM64 release runner 的 SHA-256 均与保留基线
相同，指纹记录在 JSON 中，确认默认构建没有遗留实验实现。

## 实际比较的方案

每个补丁都能独立应用到上面的同一基线。补丁是实验材料，不参与正常编译，
也不增加可选运行后端。编译器原样升级另设对照，避免混入尾调用的效果。

| 名称 | 相对基线的改动 |
| --- | --- |
| `array` | 仅固定代码页长度、明确页内索引范围；固定页独立复核 |
| `bounded` | 固定代码页，加私有五位寄存器类型与 `assert_unchecked` |
| `outlined` | `bounded` 加独立冷解码函数 |
| `localpc` | `outlined` 加局部 PC、边界同步和有证明的指令计数 wrapping add |
| `zero` | `localpc` 加永久为零的寄存器槽 31 |
| `zeroonly` | 仅改变零寄存器槽 |
| `boundedzero` | `bounded` 加零寄存器槽 |
| `threaded` | `outlined` 改为 26 类真实指令处理器之间的 `become`，普通 Rust ABI |
| `tailcc` | `threaded` 改用 `extern "tail"` |
| `preservenone` | 改用 `extern "rust-preserve-none"` |
| `tailhot` | `preservenone` 的冷解码也尾跳，移除热处理器的常规解码调用，并证明内部索引和预算边界 |
| `compact` | 从原始字重新提取寄存器字段，16 位代际标记放入记录，40 → 32 字节 |
| `packed` | 保留预解码源寄存器，将目的寄存器和位宽合为一字节，再容纳 8 位代际标记，40 → 32 字节 |

`compact` 和 `packed` 的页身份仍使用原来的 64 位标识；缩小的只是页内解码
代际。回绕时清除整页有效性。`packed` 额外验证了即将复用代际的旧邻居不会
被误命中。记录压缩在完整缓存中可把每 vCPU 的指令记录从 5 MiB 降到 4 MiB，
这项内存收益不能等同于执行提速。

## 汇编与寄存器发现

- 基线 ARM64 缓存循环已经让 PC、预算、计数及部分指针驻留在寄存器中；
  guest 通用寄存器仍按动态编号从数组加载、写回。源码中的局部变量并不保证
  都留在宿主寄存器里，增加局部 PC 的实验没有显示出稳定的通用收益。
- 抽查的标量加法、乘法和高位乘法已经对应 AArch64 的本机算术指令。
  单次 guest 运算周围仍有操作数读写、位宽处理、分派和退出条件；把算术本身
  改写成同一条汇编指令不能消除这些工作。
- 普通尾跳不等于没有保存/恢复开销。ARM64 的 `tailcc` NOP 处理器仍有
  144 字节帧；`preservenone` 降到 64 字节。将冷解码改成尾跳之后，`tailhot`
  NOP 处理器不再建立栈帧。访存和完整语义回退仍可能调用普通函数。
- `tailhot` 仍要读取 CPU 状态、检查预算/页边界并分派下一条指令；它没有
  把 31 个 guest 通用寄存器全部绑定到宿主寄存器。其代码体积也比普通循环大。

专用调用约定来自真正的 nightly：
`1.101.0-nightly (4ebd25825 2026-10-15)`，LLVM 23.1.4，发布通道日期
`nightly-2026-10-16`。使用 `explicit_tail_calls` 与 `rust_preserve_none_cc`；
`tailcc` 对照使用 `rust_tail_cc`。没有使用 `RUSTC_BOOTSTRAP`。
稳定对照为 Rust 1.90.0 / LLVM 20.1.8。两边均为 release、默认 opt-level 3、
Thin LTO，保留 release overflow checks。

## 测量方法与结果

共享 x86_64 开发机，单线程负载固定 CPU 0，多核负载允许 CPU 0–3。
同一阶段的新旧程序交替运行，正式复核各三次取中位数。没有本任务的编译或
其他测试与计时重叠；仍无法控制宿主频率及其他租户，不能把小差异当作确定收益。
宿主 CPU 时间也记录在数据中；不能把这里所有抖动都归因于 I/O 或抢占。

CoreMark 使用同一份上游默认单线程 `-O2` guest 程序，5000 次迭代，每次均
通过 CRC 校验且计分超过 10 秒。Vim 保留默认启动行为；Python 和 GCC 每次
使用新复制的相同 store、原版签名包及相同 hybrid musl。未修改 guest 时钟。
短访存循环波动较大，因此后续复核改为同一循环的 1000 万次构建，保留其
结果检查和新旧 guest 指令计数一致性检查。

固定代码页的独立三次复核，使用 stable 基线：

| 工作负载 | 基线秒 | `array` 秒 | 基线耗时 / 候选耗时 |
| --- | ---: | ---: | ---: |
| CoreMark | 16.67600 | 21.67500 | 0.769× |
| 1000 万次访存循环 | 2.90536 | 2.62774 | 1.106× |
| Vim 默认首次光标 | 2.18142 | 2.26325 | 0.964× |
| Python 导入 / SQLite / hash | 9.36195 | 8.88264 | 1.054× |
| GCC 编译、链接并执行 | 2.78745 | 2.82732 | 0.986× |

虽然访存负载改善，CoreMark 明显退步，所以固定长度表示也只归档为实验。


nightly 原样编译的基线与 `tailhot` 的正式对照（同一个编译器）：

| 工作负载 | 基线秒 | `tailhot` 秒 | 基线耗时 / 候选耗时 |
| --- | ---: | ---: | ---: |
| CoreMark | 15.840 | 13.736 | 1.153× |
| 1000 万次访存循环 | 2.50696 | 2.15485 | 1.163× |
| Vim 默认首次光标 | 1.86603 | 1.88884 | 0.988× |
| Python 导入 / SQLite / hash | 8.12551 | 8.41962 | 0.965× |
| GCC 编译、链接并执行 | 2.58612 | 2.46453 | 1.049× |

这是整个尾调用候选相对于同版 nightly 原始生产源码的效果，不是单独
`become` 关键字的效果。只升级 nightly 的初筛没有提速。`tailhot` 相对
`nightly + outlined` 的两次交替初筛中，CoreMark 为 1.140×，因此也做了
上面的独立三次复核。不能跨不同阶段的绝对秒数拼接加速比。

32 字节记录的三次完整复核（各自同阶段 stable 基线）：

| 候选 | CoreMark | 访存 | Vim | Python | GCC |
| --- | ---: | ---: | ---: | ---: | ---: |
| `compact` | 0.986× | 0.928× | 1.022× | 0.919× | 0.976× |
| `packed` | 0.999× | 1.024× | 0.986× | 1.055× | 0.872× |

不因其中某一项好看就选用整组改动。初筛只有一至两次采样的方案保存在数据中，
用于定位方向，不当作稳定排名。全部样本计时、CPU 时间、运行时计数、输出校验、
二进制/fixture 指纹以及各阶段比较对象见 [JSON 数据](dispatch-experiments.json)。

ARM64 Linux 二进制也编译并在 QEMU 下运行过。相关计时是双重解释环境下的
代码生成观察，不是 iPad/M3 性能或硬件计数器数据，而且 nightly 对照还包含
编译器变化。没有以这些数字声称真机提速；下一步需要在同一台 iPad 上比较
普通循环与尾调用的真实应用耗时、功耗和持续性能。

## 正确性与验证范围

候选覆盖了工作区回归、独立 QEMU 差分、信号恢复、1/2/4
vCPU 与原生调用边界。具体版本和完成情况记录在 JSON 的 `validation` 中。
新增测试区分 XZR、SP、Q31，覆盖冷/热缓存与不同 slice 预算，并确认公共
寄存器接口仍拒绝越界下标。

四种尾调用阶段均跑过 release CPU 测试；`tailhot` 另外通过 22,272 组 NZCV
差分、ARM64 guest 访存程序，以及 256 KiB 宿主栈下的 1000 万次访存循环。
它们保留真实访存、syscall/native-call 退出、完整指令回退和自修改代码检查。
这些验证还不能替代完整 iPad 应用验收，原型也不作为新发布版本打包。

## 复现

原型只存在于 [补丁目录](dispatch-experiments/)。在独立 checkout 应用一个补丁，
不要把多个完整补丁叠加：

```sh
git worktree add --detach /tmp/trames-dispatch-lab 357d80c246fe744333f0e0f486a1d78cc50f3616
cd /tmp/trames-dispatch-lab
git apply /absolute/path/to/dispatch-experiments/tailhot.patch
rustup toolchain install nightly-2026-10-16 --profile minimal --target aarch64-unknown-linux-musl
cargo +nightly-2026-10-16 test --release -p trames-cpu
cargo +nightly-2026-10-16 build --release -p trames-cli --bin trames-run
```

普通循环基线须在另一个未应用补丁的相同 checkout 用同一 nightly 构建。
其它 stable 候选用 `+1.90.0`。配置 GNU AArch64 工具链及 QEMU 后，可以构建
同样的 ARM64 Linux runner。保留各 runner，再从当前 checkout 使用同一个
[执行比较脚本](../../tests/performance/execution.py) 和含有 CoreMark/Vim/Python/GCC
的原版 Alpine store 进行对照。

延长访存 fixture 的命令：

```sh
aarch64-linux-gnu-as --defsym iterations=10000000 -o /tmp/cpu-memory-long.o tests/fixtures/cpu-memory.S
aarch64-linux-gnu-ld -static --build-id=none -z noexecstack -o /tmp/cpu-memory-long /tmp/cpu-memory-long.o
taskset -c 0 python3 tests/performance/execution.py \
  --baseline /absolute/path/to/baseline --candidate /absolute/path/to/candidate \
  --baseline-commit 357d80c246fe744333f0e0f486a1d78cc50f3616 \
  --store /absolute/path/to/app-store --samples 3 --coremark-iterations 5000 \
  --cpu-memory-fixture /tmp/cpu-memory-long \
  --workloads coremark cpu-memory vim-default python-imports gcc-build-run \
  --output /tmp/dispatch-result.json
```

四线程负载另用 `taskset -c 0-3` 和 `--workloads smp-work`。测试不设置 CI 计时
门槛，不按跑分程序识别或替换 guest 计算，也不生成可执行内存。
