# Apple TF：公开 Vulkan、guest 和原版 Mesa GLES3

2026-10-21。原生 TF 已接入固定 MoltenVK `db660224` 的公开 API、pipeline、
command encoder、query pool 与生命周期；guest ICD/wire 8/profile 0x10006 保持原样。
[实现提交](https://github.com/kb5000/trames/commit/a2733a33a2317a39bf5941431b65095b3c353a66) ·
[云端验收](https://github.com/kb5000/trames/actions/runs/39598761585) ·
[输入与产物摘要](vulkan-native-tf-runtime-ci.json)。没有 GL 版本覆盖或软件渲染回退。

## 原版算法和 GPU 数据路径

- 继续使用原版 SPIRV-Cross 的 vertex-as-compute、Metal stage-in、descriptor/argument
  buffer、push constants 与原始 VS 函数体。两个入口参数方法增加 virtual 钩子；
  MoltenVK 原有配置代码由 capture 与 raster-interface 编译器共用，保留完整 resource
  base type、动态 binding、constexpr sampler 和输入映射。
- `XfbBuffer/XfbStride/Offset/Component` 由新代码反射。GPU 组装图元、并行 map/scatter，
  捕获输出后 replay 原版 raster 接口；位置的 Y/clip 修正在 replay 阶段应用。
  SSBO atomic 的计数验证 VS 不因 replay 重复执行。普通、不激活 TF 的 draw 保留原路径。
- 原始索引保留 restart 标记，硬件 stage-in 使用有效索引；全 marker 的 draw 不执行 VS。
  shader guard 在原始 body 前处理越界线程和 marker。输出行距使用实际顶点数，scratch
  字节数使用原版编译器的 Metal size/alignment 规则，不猜测 float3/矩阵/数组的 padding。
- counter 续写、byte-count 转换、indirect draw 参数、firstIndex/baseVertex/firstInstance 和
  divisor=0 输入调整均留在 GPU。正常绘制不通过 CPU 读回参数、索引或顶点来决定执行。
- 查询沿用原版 availability、完成回调与 command-pool 临时资源；补齐 TF 两个结果元素、
  非零 firstQuery 的区间和 GPU copy。deferred copy 在释放 availability 锁后编码，
  防止虚函数再次快照状态时自锁。guest 依赖图与提交快照继续保活 buffer/pipeline/query。
- 固定上游 archive、两个既有修补、TF 接线 patch、两行 SPIRV-Cross header patch、全部新
  源码和原始许可证一并保留在 GPLv3 配套源码包；pipeline cache 显式区分 TF 编译模式和 ABI。

## 实际验收

云端 Apple Paravirtual Metal GPU、ARM64 Mac；不是物理 iPad 跑分。

| 路径 | 检查 |
| --- | --- |
| 原有 Rust/native API 客户端 | 两个 strided buffer、padding、counter 暂停/恢复、indexed/base vertex/instance、written/needed query、overflow、GPU byte-count draw、删除 guest 句柄后的在途依赖 |
| 普通原生 Vulkan C 客户端 | 六种拓扑的 24 个 direct/indirect、u16/u32 restart、非零 query 编号用例；2200 个精确 TF 标量 |
| 原生 VBO/SSBO/raster | 正常 draw、u16/u32 的正负 baseVertex 与全 marker、direct/indirect divisor=0；精确 VS atomic 次数、TF 标量与 576 个 RGBA 像素 |
| GPU scratch 边界 | 超限 indirect draw 经原生 device/fence 返回失败；不静默丢弃，不读回 count 后代替执行 |
| 普通 ARM64 guest Vulkan loader/ICD | 30 个精确 TF 标量、双缓冲、实例化、查询/availability/CPU 与 GPU copy、padding |
| 原版 Mesa 25.1.9 | 真实 GLES3 context，MRT 512 个精确像素、TF 12 个精确标量；不设置 GL 版本 override |
| iOS | 同源完整 MoltenVK 静态库编译为 arm64 iOS；不是只有独立 engine 或 MSL 编译 |

新增 C 用例也经独立 Lavapipe Vulkan 参考驱动验证；Linux CPU physical device 仅用于
API/数据对照，不算 Apple GPU 结果。guest 产物按原始成功提交逐文件校验复用，未重跑源码
未变的 CPU/MMU/Linux/网络/UI、Rust 模块及 iOS FFI 检查。没有 IPA 或 Firefox 原始数据上传。

最后的[诊断修正](https://github.com/kb5000/trames/actions/runs/39601628224)仅更新三个 Cereal
completeness 的 padding 常量，新增字段此前已经正确序列化。按原始源码行位置逐项验证
patch 只多出这三个常量变更；复用上述完整 GPU/Mesa 数据结果，仅重建 Mac/iOS 原生库
并检查原版 constructor 不再打印 `MVK-BUILD-ERROR`，没有重复通过的绘制与数据测试。

## 当前边界

支持当前 guest profile 的顶点阶段、stream 0、最多 4 个 TF buffer，每个至多 64 MiB，
stride 至多 2048；32 位 float/sint/uint、矩阵、定长数组和普通 interface block 成员。
geometry/tessellation capture、多 stream、64 位 TF 输出不在当前 profile。

单次 indirect TF 使用 128K 个输入 invocation 的 GPU scratch 额度；超限通过完成回调将
原生 device 标为 lost，由 fence/device 状态报告失败。普通 direct draw 按实际数量分配，
受真实 Metal 单 buffer/内存限制。这个边界仍可用 GPU 分块或 indirect command buffer 扩展，
不能把本批通过理解为无限 draw 大小或完整 Vulkan conformance。

GLES3/TF 已打通，不代表 Firefox 已完成硬件 WebRender 的端到端验收；后者继续使用真实
窗口与 GPU 渲染验证，不以版本字符串或离屏成功代替。
