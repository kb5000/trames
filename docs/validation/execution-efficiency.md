# 通用解释执行效率：引用派发、内联与本地 COW

2026-10-16。基线为 `7de1555e3393f13164759c3ec7efbc5a27f6bed5`，
其运行时代码与上一份 [CoreMark 采样](coremark-profile.md) 的 `2d078fd` 相同。
此次改动用于全部 guest 程序，没有针对 CoreMark 的指令识别、函数替换或路径分支。
仍只有一个 cached 解释器，无 JIT。

## 改动

- 热命中时直接借用解码记录，执行分支读取需要的操作数；不再先按值搬运完整
  `Instruction`。明确内联唯一调用点的执行器，避免辅助函数变大后 LLVM 自动
  外提执行器，增加每条指令的 ABI 调用。懒解码、已执行相邻指令的合并、预算、
  故障顺序和代码失效条件保留。
- 对寄存器、位宽、移位和逻辑辅助函数添加内联提示；加减和条件判断强制内联，
  使编译器能在各调用点消去不需要的 NZCV/SP/宽度分支。未改变算术或标志公式。
- 协调线程独占的 `Memory::Page` 改用 `Rc` COW。这个地址空间本来就包含
  `Rc<RefCell<Memory>>` 和本地 backing，不能跨宿主线程。每次普通 CPU store 和
  本地原生写入不再执行 `Arc::make_mut` 的原子引用检查；实际 fork 后仍先复制。
- 多核视图继续使用独立的 `Arc<ConcurrentPage>` 与区域同步。snapshot 传递字节，
  不传递协调线程 Rc 或借用；join 后仍通过 COW/backing/dirty/code hooks 提交。
  扩展已有测试，使父子进程的 shared 原子更新和 private COW 写入发生在不同宿主线程。

此次没有删除权限、EOF、跨页故障、ARM 原子/屏障、共享映射或自修改代码检查，
也没有引入新的 unsafe 边界。解码 entry 仍为原有布局；紧凑表示留待进一步测量。

## 性能对照

同一台 Xeon Platinum 8573C 开发机，Rust 1.90 release、thin LTO，三个交替样本的中位数：

| 工作负载 | 旧版秒数 | 新版秒数 | 加速比 |
| --- | ---: | ---: | ---: |
| CoreMark，5000 次 | 23.972 | 22.372 | 1.072× |
| 普通 CPU 访存循环 | 0.268 | 0.245 | 1.090× |
| 四线程计算，4 vCPU | 0.567 | 0.481 | 1.178× |
| Vim 正常启动 | 2.383 | 2.402 | 0.992× |
| Python 导入与 SQLite/哈希任务 | 11.051 | 10.803 | 1.023× |
| GCC 编译、链接并运行 | 3.689 | 2.963 | 1.245× |

**GCC 编译负载约提升 24.5%；计算/访存约提升 7%–18%。Vim 与 Python 基本持平。**
加速比是旧耗时除以新耗时；1.245× 对应耗时下降约 19.7%，不等于耗时下降 24.5%。
以上是观察结果，不是置信区间或通用/iPad 提升保证。原始样本有明显波动，
例如 CoreMark 旧版 23.905–31.416 秒，新版 22.182–25.979 秒；四线程计算的
单次差异也很大。不要把本轮绝对分数直接与上一批 264 分横比，或用旧原生分数
重新计算“总效率”。没有在此轮重复原生 CoreMark。

**ARM64/QEMU 单独记录：** 普通访存三次中位数 4.006 → 4.009 秒，基本持平；
正常 Vim 启动只做一个对照对，22.963 → 19.460 秒。QEMU 在 x86_64 上执行
ARM64 runner，不能代表物理 ARM64 的分支预测、缓存或 iPad 提升。

完整 [JSON 数据、输出、runtime 指标与指纹](execution-efficiency.json) 保留全部正式样本
以及未采用版本的有效诊断行。前者没有与本任务的编译或其他性能测试重叠。
后加的宿主子进程 CPU 时间计数仅出现在后续 ARM64 对照记录中。

## 正确性验证

- 全仓库、全 feature：342 项 Rust 测试；fmt、严格 all-target/all-feature Clippy。
- ARM64 runner 在固定 QEMU 下执行 CPU、memory、accel：108 项测试。
- 完整标准库 ThreadSanitizer：memory + accel 共 70 项测试。
- 独立 QEMU：基础 71 组对照与 3 个故障检查；M1 整数/访存 67 组；
  atomics/CRC 41 组、2572 个状态快照。
- 真实 musl 的 CPU 故障信号/上下文恢复；内存和磁盘 mmap、fork/COW、SIGBUS、
  discard/remap、memfd、代码别名失效。
- 1/2/4 vCPU pthread、TLS、LSE/LLSC/CASP、发布/屏障、fork/exec、共享 futex；
  同页独立区域访存；原生 pthread 的 19 个入口、嵌套回调和不同栈/存储配置。
- iOS ARM64 core/distro/FFI 编译检查。未在物理 iPad 上测量。

开发环境首次 workspace 测试因 sandbox 禁止创建 loopback socket 而失败；启用
测试所需的网络权限后全部通过。ARM64 Linux 静态链接使用已固定原始 musl 的
空 `libdl.a` 补齐本地交叉工具链，不修改运行时代码。

## 方法与复现

新工具 [execution.py](../../tests/performance/execution.py) 接受固定的旧/新 runner
和同一份 store。每个程序交替运行三次；每次重建 store 副本，固定
`PYTHONHASHSEED=0`。Vim 使用正常启动配置并响应终端探测，以首次显示光标为终点；
Python 使用现有 asyncio/ssl/sqlite3 等导入与小型正确性任务；GCC 编译并执行现有 C
样例。应用使用同一份完整 hybrid musl、pure 加速，实验 allocator 默认关闭。
CoreMark 和静态 CPU/pthread fixture 使用各自原始 libc。

```sh
python3 tests/performance/execution.py \
  --baseline /path/to/baseline-trames-run \
  --candidate target/release/trames-run \
  --baseline-commit 7de1555e3393f13164759c3ec7efbc5a27f6bed5 \
  --store /path/to/prepared-store \
  --output target/execution-efficiency/results.json
```

Store 必须包含原版签名 Alpine Vim/Python/GCC，以及在
`/root/coremark/coremark.exe` 的原版 CoreMark；源码/默认构建方式见前次记录。
先构建现有 `cpu-memory` 和 `smp` fixture 与完整 hybrid musl。
ARM64 Linux runner 的编译检查可增加 `--emulated-host`，只选择需要的工作负载。
脚本逐次保留输出、校验结果和 runtime 指标；不设置 CI 时间阈值。

初次六个 CoreMark 运行通过 CRC 验证后，诊断脚本错误地要求全进程总指令数相同；
打印不同耗时会产生少量指令差异。已修正为只对固定汇编 fixture 要求完全一致，
并改为逐次保存记录。该批只作为初步诊断，不混入最终结果。另一个辅助函数强制内联、执行器却被
LLVM 外提的中间版本，在 CoreMark/访存循环中出现退化；最终明确内联执行器。
中间版本后续与构建重叠的运行不作为性能证据，最终批次在其他任务停止后进行。

## 后续

下一步仍以 CPU 热路径为主：测量更紧凑的解码记录和冷解码代码分离，减少普通访存
重复取页/状态查询，并在物理 ARM64 上确定分支和缓存成本。当前修改没有处理系统调用
触发的多核汇合、worker 的区域访问成本或 allocator 的原生入口碎片化。
