# Apple 原生 GPU transform feedback

2026-10-21；**公开 Vulkan、普通 ARM64 guest 和原版 Mesa GLES3/TF 已接通**。
当前接线、实际 GPU 数据与完整 iOS 驱动验收见[公开 API 验收](vulkan-native-tf-runtime.md)。

以下保留此前独立内核的实现和验证记录，当时尚未开放 guest 能力。
已经通过的 divisor、depth/stencil、查询与 Linux 原版 Mesa GLES3 结果保持原样，见
[divisor 验收](vulkan-divisor.md)。本批只验证新增编译器、Metal 数据路径与 arm64 iOS 编译。

## 原版算法和数据路径

`crates/trames-vulkan/native/transform-feedback/compiler.cpp` 继承固定的原版
SPIRV-Cross `6c09849`，沿用其原版算法。接线版仅将两个入口参数方法改为 virtual，
为捕获 kernel 添加运行时参数；原始 archive 与算法体保留。顶点 shader 使用 MoltenVK 已用的
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

## 接线前的历史清单（已完成）

以下是独立内核阶段的接线清单；已通过原版 MoltenVK converter/command 路径接入，
无需另加一套 Rust native backend。当前状态以[公开 API 验收](vulkan-native-tf-runtime.md)为准：

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

## 云端 Metal / iOS 验收通过

[5ef5924 / CI 39587980561](https://github.com/kb5000/trames/actions/runs/39587980561)
全部通过。实际 Apple Paravirtual GPU 的 16 组图元/边界/原生 Vulkan 对照、u16/u32
索引、counter 续写与 byte-count 转换，共核对 **2080 个精确 TF 标量、166 个 map 条目、
1024 个 RGBA 像素**。SSBO atomic 次数确认原始 VS 每个输入调用只执行一次，replay
没有重复 shader 副作用。普通路径的并行 map 和 restart 的 GPU 扫描均经实际执行。

同源原生引擎已编译为 arm64 iOS，四份实际生成的 MSL 也全部编译为 iOS AIR；
Xcode 16.4 / Build 16F6、iOS SDK 18.5，部署目标 iOS 15。不是物理 iPad，也没有 IPA。
首轮通过的原版 compiler/MSL bundle 逐项检查后直接复用，只增加真实 native Vulkan
参考与剩余 Apple 检查，未重复编译和运行已通过的原版 compiler 或既有 guest 验收。

两个 evidence ZIP 的 SHA-256/全部 CRC、内部 12 个 checksum、四份组合 MSL 与仓库
assembly shader 的逐字节一致性，以及 iOS object 的 arm64 Mach-O 均已核对。
[证据摘要](vulkan-native-tf-ci.json)保留输入摘要、复用来源、计数、工具链与尚未完成的
公开 API 边界。**guest 仍不报告 Apple TF，Apple 原版 Mesa GLES3 仍未开放**；
不要把这批内部 Metal 内核验收等同于完整 Vulkan/Mesa TF 已接通。
