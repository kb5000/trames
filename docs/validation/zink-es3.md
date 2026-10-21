# ES3 首批：原生 MRT、宽格式与剩余缺口

2026-10-21，继续[Zink 路线](../architecture/zink.md)。目标是原版 Mesa/Zink 经
Vulkan/MoltenVK 使用 GPU，最终让 Firefox 实际启用硬件 WebRender。本批没有修改
Mesa、Firefox 或驱动版本字符串；原版 Mesa 25.1.9 仍只提供 GLES2 context。

后续的 [图像/子资源批次](zink-images.md) 已继续实现 array/cube/3D/mipmap、
原生 blit、局部 depth clear 与比较 sampler；下文记录首批 MRT 的原始边界。

## 已实现

- legacy/renderpass2 的单 subpass 支持最多 8 个颜色输出，受真实 driver limit
  限制；另可附加一个 D16/D32 深度附件。支持未使用的输出槽，已使用的附件按
  颜色顺序紧凑排列，再附加可选深度；任意附件重排、resolve、多 subpass 和 MSAA
  仍不支持。零颜色的深度 pass 与空 framebuffer 也可通过同一路径描述。
- bound/imageless framebuffer、逐附件 clear 与 load/store/layout、按输出槽格式
  匹配的 renderpass 兼容性、逐输出 blend/write-mask。不同混合状态仍要求原生
  independentBlend feature。原生异步 pipeline compiler 持有复制后的数组；
  guest 指针不进入 driver，数组不会在 worker 执行中悬空。
- 每次 command recording 和提交快照保留所有实际绑定的 view/image/memory；
  guest 先删除句柄也不会提前销毁在途附件。没有增加整次绘制或 CPU 访存的锁。
- 支持核心未压缩 8/16/32 位通道与常用 packed color 格式，覆盖 R/RG/RGB/RGBA、
  normalized、integer、sRGB、half/float；按真实 driver 查询其 sampling、filter、
  attachment/blend、storage、texel-buffer、vertex 能力。64 位通道、压缩块和
  stencil 不在本批范围内，深度仍限 D16/D32。
- buffer/image copy 按实际 texel 字节数计算带 padding 的末行边界，验证颜色或
  depth aspect；image/image copy 要求兼容的 texel 大小，mutable view 拒绝改变
  texel 大小。原先固定 4 字节的边界计算不能用于新格式。

这是既有单层、单 mip、2D、单 sample profile 的扩展，没有因增加格式枚举而
顺带支持 array/cube/3D/mipmap。实际 native format features 仍会过滤不支持的格式。

## 验证

Linux x86_64 开发宿主，真实 Lavapipe Vulkan 参考；它是 CPU physical device，
原版 Zink 只在显式 `TRAMES_ZINK_CPU_REFERENCE=1` 时选择它。此结果不能替代
Apple Metal 或物理 iPad GPU 验收。

基线探针取得原版 `OpenGL ES 2.0 Mesa 25.1.9`、最大颜色附件数 1，四输出检查
正确失败。候选取得真实最大颜色附件数 8；同一原版 Mesa 的 GLSL fragment shader
完成四目标 dense 绘制与带 `GL_NONE` 空位的 sparse 绘制。两轮共 512 个像素均
正确，禁用的目标保持旧颜色；没有只修改 limit 后跳过实际绘制。

扩展的原生 `renderpass` 回归通过 8 个颜色加深度、legacy/renderpass2、
bound/imageless 四种组合，检查每个附件的全部 GPU 像素；还覆盖缺少 clear、
错误附件数量和在途附件删除。另对 11 种本机支持的 normalized/float/depth 格式
做 buffer→image→buffer 往返，先在 GPU 上清空原 staging 数据，检查精确 texel
与未写入的尾部，避免两次复制都没有发生却误判成功。

Vulkan 19 项 lib 测试、严格 Release Clippy（全部 targets）、C fixture 的
`-Wall -Wextra -Werror`、Rust 格式/Python 语法和差异检查通过。复用源码未变的
CPU、Memory、Linux、网络与 UI 检查；旧 compute/timeline 等不重复运行。

`zink-es3.py` 直接打开显式指定的临时 Mesa store，不复制整个 rootfs；导入唯一
路径的 fixture/ICD 并在结束后清理。总提交和单进程各 4 GiB，4 CPU，离线。

```sh
python3 tests/integration/zink-es3.py \
  --store target/zink-mesa/store --output target/zink-mrt-new --stage mrt
python3 tests/integration/zink-es3.py \
  --store target/zink-mesa/store --output target/zink-es3-new --stage all
```

输入由 `prepare-zink-guest.py` 使用原版签名包/headers 构建。`--stage all` 包含
真实 ES3 context、MRT 和 transform feedback 数据验证；**当前仍返回失败**，
原版 EGL 报告 ES3 configs=0。MRT 通过没有被记成 ES3 已通过。

CI 使用精确源码 hash 限定本批范围，复用未变模块。Linux 准备原版 Mesa store，
将同一 guest ICD/fixture 及逐文件校验清单交给 Apple；Apple 再做原生 Metal
附件/格式读回、原版 Mesa MRT 和 iOS FFI 编译。没有构建 IPA 或假定物理 iPad
已经通过。

源码 [a02663a](https://github.com/kb5000/trames/commit/a02663ac0b90f2cc1ec324b546f51ec9641b176b)
的 [CI 39536470296](https://github.com/kb5000/trames/actions/runs/39536470296)
已经全部通过。Apple/MoltenVK 在真实 Apple Paravirtual GPU 上完成同样的
8 色加深度四组合读回、11 种格式往返、原版 Mesa dense/sparse MRT 512 像素。
Apple Vulkan 19 项测试、严格 Clippy 和 aarch64-apple-ios FFI 编译通过。
这是云端 Metal GPU，物理 iPad 尚待验证；未运行未改动的 CPU/网络/UI 全套，
没有 IPA。后续仅追加本记录，复用该提交的精确源码结果。

## Firefox 与 WSI

另测试上游 `gfx.webrender.software.opengl=true`，保留默认刷新率、内容沙箱和
Wayland proxy。目标 JS 标题约 180 秒出现，但只得到两个旧 SHM revision，且
原版 Zink 报告 swapchain 创建失败。该组是 JS/title-only gate，未回读页面
canvas；不能据报告中的 JS 通过声称 GPU 页面已显示或计算性能提升。

失败日志指向 `vkCreateWaylandSurfaceKHR` 的连接认证：当前只接受直接连接原生
Wayland 服务的 guest FD，Firefox 代理后的 FD 不对应同一 local channel。
后续需保留客户端连接/对象身份边界，设计可穿过正常 Wayland relay 的原生认证
capability；不能把任意 socket FD 或仅一个 object ID 当作被认证的客户端。
本批未放宽认证，也未默认关闭 Firefox proxy。原始 Firefox 日志、截图和运行
转储只保留本地，不纳入公开提交或 CI 输入包。

仅用于诊断关闭 proxy 的对照不再出现该 surface 创建错误，脚本可完成，但
240 秒仍无目标 canvas 或新页面 frame。另一个 150 秒调用跟踪组明确观察到
surface、swapchain、acquire、graphics pipeline、draw 与 queue submit，尚未
观察到 `vkQueuePresentKHR`。因此后续除保留代理的连接认证外，还需定位首帧从
绘制/上传到 EGL swap/present 的交接；不能把没有画面一律归因于代理，也不能
在未确认调用停留位置前断言是 GPU fence 死锁。诊断没有改变产品默认 proxy、
刷新率或浏览器二进制。

## 完整 ES3 的后续

Mesa 上游 [`compute_version_es2`](https://gitlab.freedesktop.org/mesa/mesa/-/blob/mesa-25.1.9/src/mesa/main/version.c)
要求纹理/格式、transform feedback 与至少 4 个颜色附件等能力。MRT 只是其中
一项；array/cube/3D/mipmap 与相关复制/视图已由后续[图像批次](zink-images.md)
补齐。[后续查询/feedback批次](vulkan-feedback.md)已接通原生支持该扩展的driver，并验证真实
捕获值、counter、溢出与GPU间接绘制；Linux原版Mesa的ES3 configs仍为0。
Apple GPU transform feedback与剩余版本前提仍需逐项完成。

原版 Zink 以 `VK_EXT_transform_feedback` 作为 stream-output 能力来源；
MoltenVK 1.4.2 没有该扩展，简单转发宿主扩展不能解决 iPad 的 ES3。可复用的
GPU 实现基础是上游
[`MVKGraphicsPipeline::addVertexShaderToPipeline`](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.2/MoltenVK/MoltenVK/GPUObjects/MVKPipeline.mm)
的 vertex-as-compute/capture-output 路径与
[`SPIRV-Cross capture_output_to_buffer`](https://github.com/KhronosGroup/SPIRV-Cross/blob/vulkan-sdk-1.4.309.0/spirv_msl.hpp)。
当前这些服务于 tessellation，不等同于已支持 transform feedback。

后续设计需要覆盖输出布局/stride、带界限的 buffer 写入、indexed/instanced 的
输出顺序、primitive restart、暂停/恢复、counter/overflow/query 和 GPU 可见性。
优先复用上游转换器与原生 GPU command 路径，验证实际输出与边界后再宣告 feature；
不是只添加扩展名称或让 transform feedback 空返回。
