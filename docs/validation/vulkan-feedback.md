# 原生 GPU transform feedback 与查询池

2026-10-21。本批在 main 单线扩展 Vulkan bridge，公开入口从 174 增至 188；
wire revision 8/profile 0x10006 保持兼容，新增独立操作 137–149。

## 已实现

- `VK_EXT_transform_feedback` 的六个命令、Features2/Properties2 和 device feature
  启用。只有 native driver 实际提供扩展且 transformFeedback 为真时才报告；
  最多 4 个 buffer、stream 0，geometryStreams 与 rasterization stream select 不暴露。
- GPU buffer 绑定、byte offset/size、开始/结束捕获；可选 GPU counter 读写用于暂停后续写。
  byte-count 间接绘制直接读取 GPU counter，不下载顶点或计数器来代替绘制。
- QueryPool create/destroy、command reset、begin/end（包括 indexed stream 0）、timestamp、
  CPU results 与 GPU copy。类型为 timestamp、nonprecise occlusion 或实际可用的 TF statistics；
  pipeline statistics、precise occlusion、host query reset 仍未提供。
- 32/64-bit、availability、result stride/tail 与 NOT_READY。CPU WAIT 每次仅做 native poll，
  在 guest nanosleep 让出线程；即使 WAIT 与 PARTIAL 组合，也等待完整结果。
  GPU WAIT 作为队列命令，不占 CPU worker 等待。
- Buffer usage 和 pipeline/semaphore stage 按启用能力扩展；不启用 TF 的 device 不能使用
  TF buffer/stage。vertex-only rasterizer-discard pipeline 可省略 fragment shader；
  primitive restart 扩展至 line strip/triangle fan，原先 triangle strip 保留。
- 所有对象仍经原有依赖图及提交快照保活。guest 指针、pNext 或 host handle 不互相泄露；
  不增全局算法锁，不用 CPU 解释 shader，不新增 CPU JIT。

## 本地通过的实际结果

Linux x86_64；原版 Vulkan loader + Mesa lavapipe，仅作为 API/数据参考，**不是 iPad GPU 跑分或验收**。
24 项受影响模块测试、严格 Clippy 通过。

`cargo run -p trames-vulkan --example feedback --release -- tests/fixtures/bin/vulkan-feedback.spv`：

- GPU timestamp 的 32/64-bit copy、availability、未就绪轮询、padding/tail 保留；
  销毁仍被录制命令引用的 query pool 后，提交与 GPU copy 正确。
- 两个 TF buffer、vec4/scalar 与不同 stride/offset、暂停后 GPU counter 续写、indexed draw、
  base vertex/instance，捕获的浮点值与 query results 正确。
- GPU byte-count 间接绘制；输出只能容纳 2 个点，written=2/needed=6，写入计数分别为
  48/16 bytes，界限以外的哨兵未变；销毁 pipeline/shader/framebuffer/query handles 后仍能提交。

普通 `vulkan-feedback.c` 同源 host 与 ARM64 guest 经未修改 Vulkan loader 均通过：
两个输出 buffer 共 30 个精确 GPU scalar、实例序号、query 统计与 GPU copy 一致。
guest 端覆盖新增 ICD/pNext/序列化路径，使用当前解释器与原有 NativeCall。

复现：

```sh
python3 tests/integration/vulkan-feedback.py --store target/zink-inputs/store \
  --output target/vulkan-feedback-check --require-feedback \
  --native-client target/zink-inputs/vulkan-feedback-host
```

输出报告保留 binary/ICD/shader SHA-256、明确的 capture/unavailable 结果和 cleanup 状态。
同一 store 不并发使用，不复制大 rootfs。CI 仅运行受影响 Vulkan/原版 Mesa 与 Apple embedding，
CPU/MMU/Linux/网络/UI 等既有通过结果复用。原始 Firefox 数据不上传。

## Apple 与 GLES3 的实际边界

**这不是已完成 iPad transform feedback。** 固定原版 MoltenVK 1.4.2 不提供该扩展；
Apple 侧应验证查询池和未支持能力的拒绝路径，不能把 unavailable 计为捕获成功。
本地原版 Mesa 25.1.9 仍返回 GLES3 configs=0；没有覆盖 GL 版本、伪造扩展或替换为软件 OpenGL。

检查固定 SPIRV-Cross `6c09849` 的 `spirv_msl.cpp` 后确认：`capture_output_to_buffer`
生成的是 stage-output struct 数组；它没有按 SPIR-V `XfbBuffer`/`XfbStride`/`Offset`
自动实现 TF 写入。仅打开现有 tessellation 的 vertex-as-compute 开关并不够。

下一阶段复用 MoltenVK 原版 vertex-as-compute/capture 输出和 native Metal command 路径，
需要补齐 GPU 输出布局/组装、counter/overflow 与 shader 到 rasterization 的交接。
Indexed、instanced、primitive restart/strip 的输出顺序与界限须保持；shader 中的 SSBO/atomic
副作用应只执行一次，不能用重复执行 vertex shader 来蒙混 rasterization 与捕获。
GPU counter 转换与间接参数也应留在 GPU。原版源码和最小 patch 随构建配套保存。

本批没有改动 MoltenVK 算法或原版 Mesa。云端 Apple 原生结果以本批 CI 为准；物理 iPad 尚待测试。

首轮 CI [39559895800](https://github.com/kb5000/trames/actions/runs/39559895800) 的 Linux
API/查询/捕获、原版 Mesa 回归，以及 Apple 模块/24 tests/iOS FFI 已通过。Apple query
进一步暴露出原版 CPU 时间戳 fallback 与 GPU copy 不一致：CPU 为非零完成时间、GPU
copy 为 0。用[原版源码能力补丁](moltenvk-source-build.md#时间戳能力门控)限制为真正具有
Metal timestamp counter set 的设备；无硬件计数器时拒绝 timestamp query creation，仍验证
occlusion 的 32/64-bit results、GPU copy 和 availability，不把不存在的 GPU 时间戳算通过。
重试只运行改动后的 query/feedback clients 与 Mac/iOS 原生库构建，复用已通过且源码未变的
模块、图像/Mesa 与 iOS FFI 验收，避免重复通过项目。

另一个已确定的 ES3 前提是 `ARB_depth_buffer_float`。原版 Mesa `st_extensions.c`
要求 `Z32_FLOAT` **和** `Z32_FLOAT_S8X24_UINT` 的采样/深度附件支持；当前 bridge 只支持
独立 D16/D32，完整 depth/stencil 路径必须补齐。它与 Apple GPU TF 一起继续阻止完整 ES3。
