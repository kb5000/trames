# iPad CoreMark：原生与 guest 对照

2026-10-16，用户在真机运行并贴回完整输出。这里记录**用户提供的设备结果**；
开发环境没有远程操作或独立采样这台 iPad。用户随后提供了当前 guest 的
`run1.log` 和 `run2.log`，并明确允许记录到公开仓库。原始输出：

- 原生：[performance](coremark-ipad-native/performance.txt)、
  [validation](coremark-ipad-native/validation.txt)。
- guest：[performance](coremark-ipad-native/guest-performance.txt)、
  [validation](coremark-ipad-native/guest-validation.txt)。
- [结构化数据与日志 SHA-256](coremark-ipad-native.json)。

设备为 `iPad15,4`，系统报告 `iPadOS 26.6.1`，App 为临时 `0.17.0 build 18`。
应用源码为 `93ca825ab875c0f2c483cad1a482ca91e6c8c40d`，IPA SHA-256 为
`0df9ea72b41c251eb82c5562d6f47fba35edcf4e9e263402178e7a415674efad`。
[构建与下载](https://github.com/kb5000/trames/actions/runs/38911595538)保持为
Actions 临时产物，没有发布正式 Release。

| 运行方式与输入 | Iterations/Sec | 迭代数 | 计分秒数 | seedcrc | 官方检查 |
| --- | ---: | ---: | ---: | --- | --- |
| 原生 performance：`0 0 0x66` | 37,399.488874 | 600,000 | 16.043 | `0xe9f5` | 通过 |
| 原生 validation：`0x3415 0x3415 0x66` | 36,737.692873 | 600,000 | 16.332 | `0x18f2` | 通过 |
| guest performance | 589.717472 | 11,000 | 18.653 | `0xe9f5` | 通过 |
| guest validation | 591.080064 | 11,000 | 18.610 | `0x18f2` | 通过 |

两次原生运行均为单线程、2000 字节总数据、堆分配、`-O2 -fno-lto`，由
`Apple LLVM 17.0.0 (clang-1700.0.13.5)` 编译。计时使用 `CLOCK_MONOTONIC`，
本地 Linux 暂停。低电量模式均为 false，开始与结束热状态均为 0（nominal）。
原生与 guest 的四份输出均有 `Correct operation validated`；各算法 CRC
匹配官方参考值，计分区间均超过 10 秒。输出的 `CoreMark Size: 666` 是总数据
按三个算法分配后的大小，与总计 2000 字节一致。
`crcfinal` 随输入和迭代数变化；原生与 guest 的迭代数不同，不要求最终 CRC 相同。

guest 使用 `GCC14.2.0`，报告 flags 为 `-O2 -DPERFORMANCE_RUN=1  -lrt`。
按默认单线程配置比较；日志没有多线程 `Parallel` 行，未另行记录 guest
vCPU 配置。`-DPERFORMANCE_RUN=1` 出现在两份编译参数中，但 validation 输出的
`seedcrc=0x18f2` 和算法 CRC 确认使用了 validation 输入。guest 运行的开始时间、
低电量模式和热状态未提供，不从原生运行的状态推断。

guest 日志没有 App 版本、源码提交、ELF 哈希或命令行的独立记录；按用户的连续
报告，将它们归入本次同机 build 18 对照。guest 上游源码提交未独立核验。

上游固定为 `eembc/coremark@1f483d5b8316753a742cbf5590caf5bd0a4e4777`。
算法 C 源码不变，只有 POSIX port 的时钟选择与 App 输出/入口接入；
[原生实现与复现说明](../../apps/ios/NativeCoreMark/README.md)。此前包核验确认
CoreMark 函数位于 ARM64 宿主的 `__TEXT,__text`，64 个包内文件与实际设备构建
逐字节一致，Apple CI 的 16 项模拟器检查通过。这些构建检查与本次用户真机
结果是不同来源的证据。

performance 与 validation 使用不同输入；两者相差约 1.77%，不能作为同一
负载重复运行的波动范围，也不合并计算中位数。当前原生 performance 基线约
**3.74 万分**。

按相同输入分别计算 `guest / native` 的吞吐比例：

| 输入 | guest / native 吞吐比例 | guest / native 耗时比（相同工作量） |
| --- | ---: | ---: |
| performance | **1.576806%** | **63.419333** |
| validation | **1.608920%** | **62.153497** |

本轮同机观测为 **guest 约 590 分、吞吐约为原生的 1.6%、相同工作量约慢
62–63 倍**。不同迭代数已由每秒迭代数归一化；不能直接用两份原始秒数相除。
Apple Clang 与 guest GCC、Mach-O/Darwin 与 ELF/musl、编译参数及代码生成差异
仍包含在这个端到端吞吐比较中。这不是每条 guest 指令的固定开销、周期比、
所有应用的效率或解释器可独立归因的纯开销。没有设备函数/分支/cache 采样来
进一步拆分本轮的差距。

此前用户报告 guest 370 分，其 App 版本和完整配置未保存。新 performance
数字是它的约 1.594 倍（高约 59.4%），但不能据此给出本轮单个补丁或所有优化
的受控加速比。历史 370 分与原生的约 101 倍差距不再作为当前版本比较。

后续需要复测时，保留当前 guest 编译产物，在设备冷却且低电量模式关闭时运行：

```sh
cd /root/coremark
./coremark.exe 0 0 0x66 0
```

最后一个 0 让上游自动选择迭代数；保留完整输出，确认单线程、2000 字节、
默认 `-O2`、CRC 通过且计分至少 10 秒，并补记 App 版本、ELF 哈希和运行状态。
当前各输入各有一次观测，不计算重复样本的中位数、置信区间或性能回归阈值。
