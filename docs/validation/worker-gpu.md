# GPU 调用使用调用者的 worker 和内存 View

> 本文为公开的验证汇总。提及的原始日志、二进制和逐项 JSON 记录保留在开发环境，未随本次源码提交公开。

2026-10-22，第九批协调成本拆分，接续 [timerfd/ppoll](worker-waits.md)。本轮仍在本地
工作树；[架构边界](../architecture/epoch-services.md#gpu-context-与外部-vulkan-调用)记录
了共享对象图的 unsafe 租约和仍保留的集中路径。

## 实现

- Vulkan backend 的 memory 参数泛化为 CpuMemory，worker 使用自己原有的 View，
  joined 路径继续使用 Memory。资源、descriptor、command、shader ABI、coherent
  shadow、ID 检查和 driver 算法仍是同一实现，没有复制一套 GPU API。
- Memory 和 View 提供同步 CPU 页片段借用。View 沿用普通 CPU 的 frame/region guard、
  quota、dirty/code/reservation 更新，不叠加 GPU 特有的权限或 guest 字节屏障。
  原纯 native 的 backing checkpoint 准入不变，跨页复制仍逐页、预检后才产生效果。
- 每 VM 的 Context 保留整个 backend 图。只锁同一个 context，不锁 session/VFS，
  不给普通原生函数加锁，也没有新增协调/服务线程。不同 context 可独立执行。
  Backend 仍 !Send/!Sync；私有 OwnedBackend 信封在独占租约内移动完整 Rc 图。
  公共 API 不泄漏 Rc/Object/借用，driver compiler/UI 只保留原有 Send/Arc 数据。
- 同步 GPU 调用验证 HLT 后在当前 worker 执行，结果、预算和 PC 只提交一次。计数和
  transfer delta 留在任务私有状态，结束时汇总；worker_gpu_calls 与 syscall 分开。
  GPU 负结果日志按任务/cohort 合并为错误次数及最后一条，保留 pid/comm/op/PC/result。
- 2/9 teardown、27/70 async compile、110/111 Wayland FD 认证在效果前回到原 owner。
  Pending 不作为同步完成处理。没有迁移 native relay pump 或其他 host socket 服务。

当前同 context 的调用仍串行，而且争用其 Mutex 的 worker 可能占着宿主槽等待。
这一步移除了普通 GPU API 的逐次全体返回；尚未完成对象/队列粒度同步，也未完成
编译结果的无轮询交接。context 锁等待释放执行槽、编译通知、FD 认证及 relay 是后续目标。

## 范围检查

Release CLI/FFI 构建、严格 Clippy（memory/accel/vulkan/core/cli/ffi，含 tests）、fmt、
diff 空白、Python 编译和 fixture shell 语法检查通过。只运行受影响模块：

| 模块 | 通过数 | 内容 |
| --- | ---: | --- |
| Vulkan | 35 | 原 wire/graphics/WSI/coherent 检查；新增 worker 跨页传输与 COW/预检、完整图跨线程移交、独立 context、owner-only 准入 |
| Memory cpu_memory | 4 | 新页片段接口与 worker LL/SC reservation、权限/跨页拒绝、dirty 提交；原共享 CPU 借用及 reservation |
| Core workers | 30 | 原 I/O/futex/native/service/预算/停止；新增 GPU 同线程、HLT 精确退休、owner 回退和非法 function 无效果（四种场景） |
| Core GPU process | 2 | GPU 等待的信号语义、已移除 context 完成一次且不重放 |

合计 **71 项通过**。Core 新 GPU 项单独通过后，其余 29 项 worker 检查跳过该项，
没有重复计数。最后 Memory 测试只把 assert_eq(bool, true) 改为 assert(bool)，复用
其已通过结果；最终严格 Clippy 覆盖此测试格式调整。原文件/Linux/futex/Unix 等没有
实现变更，按源码 hash 复用之前结果，没有重跑完整 CPU/VFS/Linux/网络/GPU 应用套件。

初始新预算断言误把非法 function 也当作已预留 HLT：原语义应在 fault 前退还未执行的
1 credit。修正测试预期后四场景通过，没有为此放宽产品预算逻辑。首轮 Clippy 的 bool
断言风格报错也已修正。保存基线时 copyfile 未保留 executable 位，首次对照在启动前
被 OS 拒绝；补上本地执行位后最终对照通过，基线内容 SHA 与上轮最终 CLI 相同。

## 原版 musl 与实际 Vulkan 驱动

新静态 ARM64 musl fixture 使用受验证的 Vulkan HLT transport：

- query：32 次 display query，检查输出边界 canary。
- fill：真实 native VkDevice/Buffer/Memory/CommandPool/Fence，32 次命令填充、提交、
  fence 完成与 HOST_COHERENT readback，每轮校验全部 1024 个 uint32。创建、销毁使用
  同一组 opaque ID，driver handles 不由 guest 提供。
- 两种同时运行另一 guest 线程，固定 1024 次 1 MiB 原生填充（1 GiB），独立校验字节
  和 checksum。固定工作量；fence status 的轮询次数依实际驱动完成而变，不能当成恒定
  guest 指令量。guest 的显式 sched_yield 沿用本地队列，不是新增的生产轮询总线。

两模式 × pinned/spread × 1/2/4 CPU，新旧各 **12 组，合计 24 组通过**。输出、校验和、
原生填充次数/字节均一致，native fault/reject/fallback 为 0。候选多 CPU 均有本地 GPU
调用，协调服务请求/完成均为 0；pinned 实测 peak_parallel_cpus 为 1。单 CPU 仍走 joined。

四 CPU 的一次结构样本：

| 模式 | affinity | epoch 前 → 后 | 候选 worker GPU 调用 |
| --- | --- | ---: | ---: |
| query | pinned | 41 → 10 | 32 |
| query | spread | 41 → 9 | 32 |
| fill/readback | pinned | 106 → 7 | 256 |
| fill/readback | spread | 230 → 6 | 771 |

GPU direct transfer 均为 38,912 bytes，buffered 为 0。这里只报告全体返回次数；尤其
fence 就绪检查次数受 host 调度影响，不用这些数据推算墙钟加速或 iPad 表现。

另外使用缓存的原版 Linux Vulkan loader、现有 guest ICD、原标准 Vulkan C fixtures
及相同宿主驱动 oracle，完全离线验证：

1. compute：默认/专用化 shader、binary semaphore 链及复用、部分映射、偏移
   flush/invalidate、remap/pointer alignment，stdout 与宿主一致；2 次 async compile。
2. coherent：持久映射、无显式 flush/invalidate、独立 CPU 输入/GPU 输出范围，stdout
   与宿主一致；GPU transfer 全部 direct。
3. render：3 次 graphics draw，stdout 与宿主一致，256×192 导出 PPM（147,471 bytes）
   与宿主逐字节一致。原 fixture 编译 peer 在绘图前结束，此次 worker_gpu_calls 为 0，
   所以此项只能证明 joined transport 的绘图兼容。
4. 为真正验证 worker graphics，链接一个只等待结束通知的额外 guest 线程，保留同 VM
   的第二个 View；原 render 主函数和 Vulkan 调用不改。**109 次 worker GPU 调用**、
   1 次 async compile、439,762 direct GPU bytes、0 buffered bytes，stdout 和图像仍与
   同一个宿主 oracle 完全一致。等待线程使用条件变量，没有 CPU burn 或定时轮询。

首次 host render 命令漏传 PPM 输出路径，触发 fixture 自身 argc 断言；仅补齐命令后
通过，未修改产品代码。没有把该失败算作 Vulkan 渲染回退。

本机驱动是 Linux x86_64 Mesa lavapipe，实际运行 Vulkan driver，但使用软件执行 GPU
工作。没有新的 Apple/MoltenVK/真机 UI/零拷贝呈现验收，也未运行 Firefox、Docker 或
CoreMark。没有读取、归档私人 Firefox 诊断，没有 IPA、push 或 release。

## 复现

准备既有 ARM64 musl 工具链和 host Vulkan driver 后：

```sh
bash scripts/build-worker-gpu-fixture.sh
VK_DRIVER_FILES=/path/to/host-icd.json LP_NUM_THREADS=2 \
  python3 tests/integration/worker-gpu.py --runner target/release/trames-run \
  --baseline /path/to/previous/trames-run --output target/worker-gpu-results
```

已有 pinned Vulkan SDK/loader fixture 产物时，向构建脚本加 `--standard-render` 可生成
带等待线程的 worker-gpu-render；其参数与原 vulkan-render 完全相同，指定 4 CPU 和
Vulkan accelerator。宿主 oracle 继续使用未修改的 vulkan-render-host。
环境、源码/产物 hash、原始输出、像素 hash 和未变实现复用见`worker-gpu/results.json`（本地验证记录）。
