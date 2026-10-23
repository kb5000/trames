# CoreMark：build 35 → 36 的单活跃进程回退

2026-10-22。用户报告相同 CoreMark 在 iPad 上从约 680 降到 580，确认两次都只有终端。
本地同环境复现了同方向约 12.4% 的吞吐回退，并定位到并行 cohort 准入过宽。
修复已在当前代码中，尚未重新构建 IPA，也没有 iPad 恢复结果。

## 对齐实际版本

- build 35 的发布源码：`049df00bc93d1342440dc3a8ca2a8b409b217a5b`。
- build 36 的 IPA 源码：`50edf5352c56ff86f1fc3c1d0d203c1fbfe0cfea`。
- 两份实际 IPA 的 Xcode 都是 16.4/16F6，SDK 都是 iPhoneOS 18.5/22F76，构建系统
  版本同为 24G830；guest musl ELF 和 manifest 分别逐字节同 SHA256。
- CPU crate、Rust toolchain、Cargo release profile、iOS build 脚本和 FFI session
  参数处理源码没有变化。两版都为 Release、thin-LTO、同一 pinned nightly，未改成 Debug。
- 差异主要是共享资源、futex、运行队列及 GPU worker 交接；另有稀疏零页修复，未增加
  普通 CPU load/store 的 MMU 权限检查。不能因 CPU crate 没变就认定执行路径相同。

原始发布记录见 [build 35](ipad-1.3-build35.json)，本次核对的 IPA 与源码哈希见
[结构化汇总](coremark-build35-36.json)。没有读取或公开 Firefox 原始诊断。

## 同环境生产会话测量

从上述两个提交分别导出源码，使用同一个 Rust 1.101 nightly-2026-10-16、相同默认
release/thin-LTO 参数离线构建 CLI/FFI。所有构建与检查结束后再顺序测量，不与其他
编译、测试或采样同时运行。Linux x86_64/真实 4 KiB 页，宿主 affinity 固定 0–3。

使用已有 `tests/performance/session-coremark.py`，每次复制同一冻结磁盘；开启真实
多终端 FFI、Wayland、4 GiB 总/单进程预算、网络关闭，没有其他 guest 程序。CoreMark
是原版 Alpine GCC14.2 `-O2 -lrt` 生成的同一动态 ELF，上游固定为
`1f483d5b8316753a742cbf5590caf5bd0a4e4777`，15000 迭代、seeds 0/0/0x66。
输入 manifest SHA256 为 `408a8b970346fcb50bf3951a3e4769bed11f2243b52d77b95853adc23b3c7aa3`。

| 顺序 | 版本 | iterations/s | guest 秒数 |
| ---: | --- | ---: | ---: |
| 1 | build 35 | 782.840 | 19.161 |
| 2 | build 36 | 683.340 | 21.951 |
| 3 | build 36 | 687.474 | 21.819 |
| 4 | build 35 | 781.250 | 19.200 |
| 5 | build 36，仅恢复两任务 ready 准入的诊断副本 | 788.561 | 19.022 |
| 6 | 当前正式修复 | 784.765 | 19.114 |
| 7 | 当前正式修复 | 792.477 | 18.928 |

七次均满足原版 CRC 验证和至少 10 秒运行要求。旧/新/修复的分数中位数为
**782.045 / 685.407 / 788.621**：build 36 比旧版低 12.4%；修复比 build 36 高 15.1%，
恢复到旧版范围。每个正式版本只有两个样本，没有置信区间；约 0.8% 的修复/旧版差异
不当作进一步加速的证据。Linux 的绝对分数不能代替 iPad，用户实际 680→580 仍需新包复测。

## 原因与隔离

build 35 需要至少两个 Ready 任务才进入并行执行。build 36 为支持阻塞交接，改为
一个 Ready 加可接管的 I/O waiter 也能组建 cohort。App 的多终端 init 常年阻塞在
宿主写端控制的管道上；CoreMark 子进程已经关闭该管道 FD，不能唤醒它，但旧准入
没有分辨这种关系，仍将它算作并行成员。

于是单线程计算从串行 Memory 的直接 MMU 路径切到保留的 parallel View，期间还有
cohort 准备、工作槽和每轮回收。未改变 guest 指令算法，却改变了每次访存实际调用
的宿主实现。第五个样本只回改准入条件就恢复，构成具体的因果隔离；这个简单诊断
会损失单就绪任务的真实交接机会，因此没有直接采用为生产修复。

另用静态 musl wrapper 模拟：父进程等待一个控制管道，子进程关闭两端后 exec 同一
CoreMark，退出后 SIGCHLD 通知父进程。1000 迭代的短探针只看 CRC 和执行路径，
**不作为有效 CoreMark 跑分**；原版明确提示运行不足 10 秒。其结构计数为：

| 版本 | 整组返回次数 | worker chunk |
| --- | ---: | ---: |
| build 35 | 3 | 5 |
| build 36 | 152 | 18127 |
| 只恢复 ready 准入 | 3 | 6 |
| 正式修复 | 5 | 8 |

约 2.967 亿条 guest 指令基本相同，差别集中在宿主路径。build 36 的 View 准备累计
不到 2 ms，整组停止累计约 13 ms；不能把整个退化说成快照复制耗时，大部分发生在
持续计算采用了不同执行/访存路径。没有把这些计数当 iPad 的硬件周期剖析。

## 修复与范围验证

cohort 准入现在区分活跃任务与等待任务：两个活跃任务照常并行；单个 GPU 等待仍可
由 worker 交接；单 Ready 任务只有存在同 VM futex waiter 或共享字节资源的 I/O peer
时才组建 cohort。无关的空闲终端和 init 管道走原内核等待路径，计算进程保留串行
直接 MMU。没有关闭多核、强制单核或调整 CoreMark 算法、guest 时钟。

资源关联使用保留 Arc 身份，包括管道两端、Unix stream、PTY、eventfd/timerfd，以及
epoll watch 内的资源。它只是保守的调度提示，允许多算关系，不授予 FD/权限能力，
不替代真实 I/O、seccomp、信号或生命周期检查。只在单活跃任务的调度边界使用，
没有给普通 CPU 指令或访存添加检查/锁/屏障。

新增 4 项单测通过：无关 I/O 不触发 cohort、跨 VM 管道仍可交接、trace 路径、同 VM
futex/无活跃任务、单 GPU 完成，以及资源别名/对端/epoll 的关联。受影响集成只补查
10 次 guest 运行：四核 Unix read/epoll 和 PTY read/epoll 的 pinned/spread 各 4 次、
private/shared futex requeue 各 1 次；全部匹配原生输出。现有三终端 FFI 的 Ctrl+C、
尺寸、输出背压及关闭/重开通过。Release CLI/FFI、严格相关 Clippy、格式和差分检查
通过；未重跑完整 GPU/网络/浏览器验收，未重建未修改的 guest 库。

修复包含前两批尚未发包的共享 futex 和 PTY 改动；七次长样本与未改源码的 build 35/36
区分记录，未把当前工作区误当作已发包 build 36。原始测量和短探针源码留在
`target/coremark-build35-36/`，公开摘要只记录所需结果、配置与哈希。
