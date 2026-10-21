# Apple 原生 GPU transform feedback 内核

2026-10-21；这是内部 GPU 引擎，**尚未接入 guest Vulkan 命令或开放 Apple TF 扩展**。
已经通过的 divisor、depth/stencil、查询与 Linux 原版 Mesa GLES3 结果保持原样，见
[divisor 验收](vulkan-divisor.md)。本批只验证新增编译器、Metal 数据路径与 arm64 iOS 编译。

## 原版算法和数据路径

`crates/trames-vulkan/native/transform-feedback/compiler.cpp` 继承固定的原版
SPIRV-Cross `6c09849`，不修改上游源码。顶点 shader 使用 MoltenVK 已用的
`vertex_for_tessellation` / `capture_output_to_buffer` 原生计算路径；顶点输入、索引、
descriptor/SSBO/atomic 和函数体仍由原版编译器处理。普通 raster 接口也由原版生成，
再以 location/component/builtin 语义关联 capture 和 replay 字段，避免猜测 Metal
struct padding，或把矩阵/数组错误当作可直接返回的 Metal varying。

新代码只补充 SPIR-V XfbBuffer/XfbStride/Offset 的布局、GPU 图元顺序与缓冲区写入。
支持 4 buffers、32 位 float/sint/uint、Component、矩阵列和定长数组、普通接口块成员；
非零 stream、64 位输出、嵌套/数组接口块等不支持情况明确拒绝。缓冲区、stride、
输出重叠和 offset 也先验证，不生成可能越界的写入。

GPU 流程是原始 VS 一次执行 → GPU 图元 map/计数 → GPU 按字写入 TF → 从同一份
输出 replay rasterization。点、线/三角形列表、线/三角形条带、三角扇和 restart 图元
组装已实现；没有 restart 时，count 是常数时间计算，map 和 scatter 为并行 GPU dispatch。
restart 初版扫描留在 GPU 单控制线程，后续可改为并行 prefix sum。

TF 只写完整图元；buffer 满仍保留完整 raster map。padding、绑定范围前后内容不写。
byte cursor、written/needed 64 位统计、可选 begin/end counter、续写与 byte-count indirect
转换都留在 GPU；编码过程没有 CPU readback、guest 内存复制或跨 worker 同步。
验收程序完成提交后才读回数据进行比较；这不是生产数据路径。

## 本批验收范围

- Linux：固定原始 archive SHA-256；严格 C++ 编译；原始 TF shader 与含 SSBO atomics、
  Component、vec3、uvec2、mat2、数组、整数位模式的 shader 转换；u16/u32 index 版本。
- Metal：各图元顺序、实例边界、u16/u32 restart、整图元 overflow、padding/范围哨兵；
  真正 indexed VS 的重复索引、firstIndex、负 baseVertex、firstInstance。
- GPU begin/end counter 续写、offset buffer binding、后续 draw 溢出与 byte-count indirect。
- 原始 VS 输出用于间接绘制，检查 32×32 全部 RGBA 像素；检查 SSBO atomic 次数，
  证明 replay 没有重新执行 VS。
- 同一源生引擎和生成的所有 MSL 编译到 arm64 iOS。云端 Metal 不是物理 iPad 验收。

本地 Linux 已通过原版编译和布局生成；实际 Metal/iOS 结果在本批 CI 完成后记录。
CI 复用源码未变的 [39579538279](https://github.com/kb5000/trames/actions/runs/39579538279)
中 Linux/Apple Vulkan、guest、原版 Mesa 与 iOS FFI 通过结果，不重跑 CPU/MMU/Linux/
网络/UI，不出 IPA，不上传 Firefox 原始数据。每个输入和生成 shader 的 SHA-256 在
Apple 运行前逐个核对，原始依赖 archive 和许可证保留供 GPLv3 配套源码分发。

## 接入前还需要完成

目前新模块不在 `build.rs` 中链接，原版 MoltenVK 仍不报告 TF。还需要：

1. 原版 MoltenVK 的 pipeline/encoder/command 生命周期接线，以及 TF Vulkan entry points、
   feature/property、GPU query pool 和资源/stage barrier 对接。
2. indexed primitive restart 的 VS 调用抑制（本批 restart 验收独立检查图元组装，
   不冒充完整 indexed-restart VS 已完成）、drawIndirect/byte-count 的动态 dispatch。
3. 完整 descriptor/push constant/texture 状态、多 draw/render-pass restart、pipeline cache、
   raster replay 的 viewport/clip/point-size 配置和未启用 TF 时的原有快速路径。
4. 通过现有普通 Vulkan host/guest TF oracle、原版 Mesa GLES3 TF 和 Apple 像素验收后，
   才能向 guest 报告扩展和真实上限。不能用内部 Metal 测试代替公开 API 验收。

新增独立原生 Vulkan 参考：以原有普通 loader client 派生六种 topology 的固定输入变体，
实际运行 native driver，保存捕获顺序和 query 统计供 Metal 对照。确认默认 first
provoking vertex 要求 triangle strip 奇数图元为 `[i,i+2,i+1]`、fan 为
`[i+1,i+2,0]`，不能只检查绕序。参考输出由驱动产生，不由本模块的 CPU 模型产生。
首轮 Linux 编译通过；Apple 严格编译暴露验收代码中的废弃 Metal 属性和未明确的
Objective-C id 类型，已修正。重试直接复用首轮已通过且源码未变的 compiler/MSL
bundle，只补新的 native Vulkan 参考与修正后的 Metal/iOS 检查。
