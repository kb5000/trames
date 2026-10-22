# GPU 等待直接交接工作队列

> 本文为公开的验证汇总。提及的原始日志、二进制和逐项 JSON 记录保留在开发环境，未随本次源码提交公开。

2026-10-22，第十批接通 GPU context 竞争、原生编译完成通知和冻结 Wayland FD 授权。
调用者使用原 View，在工作线程执行；等待任务交还执行槽，完成事件直接通知原队列。
已接受编译的 HLT 只退休一次，跨 cohort 的完成不会重新提交。设计与保留边界见
[架构说明](../architecture/epoch-services.md)。

本批合并前九批作为 build 36 测试 IPA。既有记录中的“未提交/未构建”描述的是各次
测量时的状态，不能视为本次构建状态。源码、二进制及证据哈希见
`gpu-handoff/results.json`（本地验证记录）。未公开任何 Firefox 原始诊断。

## 验证范围

- 本批 177 项不同单元测试通过，2 项既有 ignored。Core 全套 132 项后新增的真实 FD
  授权用例单独通过；最后的跨 cohort 诊断计数调整只重跑受影响的 8 项 GPU 和 5 项队列
  测试，不重复累计通过数量。Vulkan 37、VFS readiness 2、native bridge 4、shared 1。
- 覆盖完成早于 park、锁释放后回调、编译线程 panic/断开、取消、精确指令预算、恢复
  不重交编译、一个宿主槽等待 GPU 时仍可运行其他任务，以及只剩 GPU 等待的队列。
- 真正 native socket/dup 的冻结授权可在工作线程验证；外来 socket、复制 token 的
  memfd、关闭后 FD 复用不能得到授权，回退前没有 backend 或 guest 输出副作用。
- 严格 release Clippy、格式检查、CLI/FFI release 构建通过。readiness 生产实现从 VFS
  提取至 shared，只改变可见性；归一化源码逐字比较通过。

## 实际驱动与输出

测试宿主为 Linux x86_64，使用 Mesa lavapipe 软件 Vulkan；这不是 Apple Metal 或真机
性能验收。沿用第九批原版 signed loader、guest ICD、GLSL/SPIR-V 和原生输出参考。

最后版本的四 CPU 标准 Vulkan compute、coherent 映射和带原生 peer 的 render 均通过。
4096 元素的计算结果、持久映射行为与原生参考一致；147471 字节 PPM 逐字节一致。
这些调用包含 worker 内编译提交和完成恢复，传输 buffered bytes 均为零。
最终结构计数保存在 api-final.json；短编译期间 peer chunk 计数可能为零，不据此推断
宿主实际并行度。计数只在诊断模式启用，跨 cohort 累积，且不含手工退休的 native HLT。

原 24 次同步 query/fill 对照已通过；最后版本只追加 2/4 CPU 的 8 个受影响组合，全部
通过。四 CPU 的 query spread 为 9 轮，pinned 为 10；fill spread 为 6，pinned 为 7，
与先前结果一致。标准 compute 的最后一次样本 13→11 轮，render 111→112 轮；不作为应用
加速比。单 CPU 的 owner 路径和旧版本输出复用已有同输入验证，没有重复测跑分。

## 保留范围

同一 VM context 的 host body 仍由独占租约串行；现在竞争者让出宿主槽。
instance/device teardown（op 2/9）、新 FD 授权所需的 native relay pump、映射与进程
生命周期仍保留协调边界。常规单就绪任务、配置单 CPU 的执行路径不变。
Apple ARM64/16 KiB 页测试及 iOS Release 链接由本次 IPA CI 执行，真机交互留给安装验证。
