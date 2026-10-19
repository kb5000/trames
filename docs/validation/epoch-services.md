# Epoch 内服务与原生调用并行验证

2026-10-19，本轮基线为 `f41dd0d09a9ba3bc70e6d664b6857607bb649c1a`。
实现边界见[架构说明](../architecture/epoch-services.md)，输入、二进制和源码摘要见
[证据清单](epoch-services/manifest.json)。本轮没有生成 IPA，也没有新的 iPad/CoreMark 结果。

## 改动与正确性

worker 将自己的并行内存 View 移交协调器，CPU/解释器/native registry 留在原线程。
允许的 FD 查询及立即就绪的 pipe/embedding stdio I/O 在 epoch 内完成；只挂起调用者，
其他 worker 无需 stop/join。普通 musl NativeCall 仍在 worker 内执行。
生产执行不再逐次更新共享的 CPU/native 并发峰值原子计数，仅诊断模式开启。

确定性单元测试持有 syscall 请求，同时让另一个 worker 完成原生 memset；还验证
stop 后完成结果不重放、管道输入预检、阻塞与非阻塞 EAGAIN、SIGPIPE、Pool 退出归还
View、FD bitmap 版本失效和精确 SVC 指令预算。没有把 Rc/RefCell 标为 Send。

本地通过：Core 共 66 个测试（完整 65 个后复用未变结果，加跑包含新增测试的 14 个
worker 测试），VFS 156 个、Linux 23 个；受影响五个 crate 严格 Clippy；生产 FFI
三终端的独立 PTY、共享文件、私有 cwd/env、resize、Ctrl+C、背压、退出与取消。
旧 SMP fixture 的 1/2/4 核输出与原已验证基线一致，覆盖原子/futex/fork/COW/exec/
affinity/getcpu；本轮没有新运行 QEMU CPU oracle。已有未改的 CPU/MMU/GPU/UI 验收复用。

新增静态原版 musl fixture 在 1/2/4 核执行两个 pthread：一个做原生内存填充，另一个
做 FD 查询或管道 read/write，穿插 mmap/munmap 和 dup/close。六种组合与本机 C 语义
输出一致；每组 256 次原生填充、256 MiB，全部字节计数准确且无 fault/reject/fallback。
本机 C 是程序语义 oracle，不能替代整个 guest CPU 的差分验收。

## 固定工作量混合测试

Linux x86_64、4 KiB 宿主页、release，4 个 guest CPU、两个 guest pthread。
每组固定 8192 次 1 MiB 原生 memset（8 GiB 写入），另一个线程完成 65536 轮 FD 查询
或 pipe I/O。前后使用同一个 guest ELF、输入及验证输出，原生函数算法没有更换。

| 混合负载 | 基线墙钟 | 最终候选墙钟 | 比值 |
| --- | ---: | ---: | ---: |
| memset + F_GETFD/F_GETFL/fstat/lseek | 6.432 s | 0.503 s | 12.8× |
| memset + pipe write/read | 3.635 s | 0.333 s | 10.9× |

收益主要来自消除其他线程 syscall 引起的反复全核暂停，使原生线程能连续完成；
不是 memset 本体变快 12 倍，也不是应用整体或 iPad 的提速预测。候选仍用 `--metrics`
诊断模式，因此该表也不包含关闭共享峰值计数的生产模式收益。
这是各一组长样本：最终候选复用同一二进制、ELF 和输入的冻结基线，未重新交错基线
执行；没有统计置信区间。短探索样本不作结论。完整计数与 stdout/stderr 位于
[混合测试结果](epoch-services/mixed/results.json)。

## Docker 实测与限制

生产 FFI，4 CPU、4 GiB，冻结的 1741-inode 安装后 store 与缓存 `default-probe` 镜像，
原版默认 dockerd/托管 containerd/runc/overlay2，运行交互 TTY，同时轮询另一个终端。
无拉取下载；这不是用户完整 rootfs 或同一个 `alpine:latest` 镜像摘要。

| 样本 | daemon ready | run 到 shell 提示符 | exit/--rm |
| --- | ---: | ---: | ---: |
| 基线，第一次 | 10.214 s | 20.604 s | 1.904 s |
| 基线，反向顺序复查 | 10.211 s | 21.437 s | 1.894 s |
| 最终候选 | 10.145 s | 21.731 s | 1.712 s |

本轮 **没有证明 Docker 加速**。中间候选的 21.415 s、22.666 s 也保留在证据中，
用于说明波动；不能只挑最低值。候选与基线都在约 21 秒，iPad 十几分钟问题仍待验证。
这批服务只覆盖少量 FD/pipe 路径；Docker 大量文件、proc、网络、futex、映射和进程操作
仍会回到 joined coordinator，不能称为完整 syscall 并行化。

## 平台及后续边界

Apple CI 另行执行受影响模块、同一静态 musl fixture 的 1/2/4 核语义验收及生产多终端。
Apple 的执行结果应以关联提交的 Actions 为准；Linux 数据不代替 ARM64、16 KiB 页验收。

下一阶段先处理普通文件/网络的共享 backing 一致性和提交/完成，再拆分 VFS 元数据
同步。动态 proc/sysfs、信号/futex、映射与进程变更仍须完整状态；Vulkan stateful API
仍需按 VM/对象/队列拆分所有权。当前 VFS 服务仍由一个协调器串行处理，没有宣称两个
宿主线程能同时修改 VFS，也没有给原本线程不安全的 native 算法添加全局锁。
