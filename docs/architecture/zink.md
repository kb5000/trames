# Zink 路线与当前边界

先合并性能PR #31（main121cdc7），后续在main单线推进。
[Issue #32](https://github.com/kb5000/trames/issues/32)跟踪Zink完整链路。

目标是原版Linux ARM64 OpenGL/EGL应用→原版Mesa Zink/NIR/SPIR-V→原版Vulkan
loader→Trames guest ICD→现有原生Vulkan/MoltenVK→Metal。保留上游算法，不另写一套
OpenGL实现。后续窗口继续使用现有Wayland合成器和App浮窗/标签页。

当前已在Linux原生Vulkan参考上跑通原版Mesa25.1.9的EGL1.5、GLES2 context、GPU清屏、
GLSL着色器绘制和重复纹理采样。**这是基础离屏GL链路，不是完整OpenGL或Vulkan一致性声明。**
本批Apple/MoltenVK实测结果见[验证记录](../validation/zink-gles.md)；物理iPad和Wayland EGL窗口仍待验收。

## 已实现

- 实例查询API1.1、单设备group，以及Properties2/Features2、格式、图像格式、内存、
  queue family、sparse格式查询；KHR properties2别名可用。
- physical device仍报告原有受限1.0 profile。实例接受面向更高1.x API的应用请求，
  由设备报告约束实际使用版本，不把所有宿主能力直接暴露给guest。
- 已知基字段继续使用既有受约束的native查询。未知输出pNext结构保持原样，guest链
  不传入宿主。未实现外部内存/栅栏/信号量import/export，查询报告零可用handle类型。
- 支持Features2作为device-create输入，仍只允许原profile支持的base feature。
  重复feature来源、未知输入链或不支持的feature显式失败。
- `VK_KHR_get_memory_requirements2`：buffer/image/sparse查询和core/KHR别名。
  当前image creation没有sparse，合法图像的sparse要求数量为零。
- `VK_KHR_descriptor_update_template`：guest模板复制布局与offset/stride，在guest
  ICD展开为原有native descriptor写入；支持现有uniform/storage buffer、sampler、
  sampled image、combined image sampler及数组。未知descriptor/push-template失败。
  资源由原有descriptor/in-flight依赖图保留；不新增整次调用的宿主锁。
- `VK_KHR_maintenance1`：仅在原生driver支持时枚举，逻辑device显式启用；静态与
  动态负/零高度viewport，以及原生TrimCommandPool。Trim保留已录制命令。
- `VK_KHR_timeline_semaphore`：真实64位counter/initial value、host signal、wait-any/all、
  timeout、混合binary/timeline queue submits及core/KHR别名。feature在device创建时
  显式启用；未启用时不允许创建timeline semaphore。等待查询原生driver后让guest调度。
  未排入队列的未来host signal会先轮询，避免driver阻塞唯一worker；同次提交和此前
  已入队的GPU signal会计入依赖，不强制等到GPU执行完才提交后续工作。
- 原生timeline完成后可立即reset/re-record command；私有记账fence与提交时资源快照
  保留到原生fence完成，避免释放在途资源或稍后覆盖新录制状态。
- `VK_KHR_driver_properties`：真实driver ID/name/info用于Zink的MoltenVK工作路径，
  timeline properties返回真实最大值差。guest pNext不跨宿主边界。bridge未通过Vulkan
  conformance，因此返回的conformanceVersion为零，不继承宿主证书。
- `VK_KHR_create_renderpass2`：原生KHR创建/开始/结束命令及core/KHR别名，当前仍是
  单subpass、一个颜色附件、可选D16/D32深度，单层/单sample。没有可用的next-subpass
  转换；非零viewMask、多个subpass、input/resolve attachments尚未支持。
- `VK_KHR_imageless_framebuffer`：原生feature查询/显式启用、附件格式/usage/尺寸约束，
  framebuffer创建时不绑定图片；legacy或renderpass2 begin时绑定实际view。每次recording
  独立保留这些view及image/memory，提交快照沿用原依赖图，reset后不会提前释放在途资源。
- 依赖扩展`maintenance2`、`multiview`、`image_format_list`按原生能力枚举。multiview三个
  feature均为false；只接受零viewMask。支持point-clipping原生属性、image-view usage、
  两种maintenance2 depth/stencil layout及image-format-list创建/查询链。其余图像限制沿用原profile。
  宿主为renderpass2/imageless启用相应原生依赖，不要求客户端重复列出已提升的依赖名称。
- 原版loader在1.0应用路径可能重复追加properties2扩展；相同扩展名称按一次启用处理。
- App自带ICD JSON为实例1.1；旧JSON原子刷新，保持未变库inode与用户数据。

## 本批GL/资源扩展

- 布局可见性支持`ALL_GRAPHICS`/`ALL`，不因此启用geometry/tessellation执行。
  单颜色附件/单viewport的实际限制如实报告，兼容render pass按格式/深度匹配，
  允许不同load/store/layout的兼容对象共用framebuffer/pipeline。
- 基础features按driver查询并显式启用：anisotropy、logicOp、fillModeNonSolid、
  shaderClipDistance、independentBlend。`EXT_scalar_block_layout`同样按真实driver能力查询/启用。
  polygon/logic state交给原生pipeline，未知feature继续失败。
- 原生texel buffer view、uniform/storage texel descriptors、storage-image descriptors，
  dynamic uniform/storage buffer descriptors及其template展开。动态偏移按binding/array顺序处理，
  验证对齐和有效范围；不增加算法锁。绑定与提交快照保留view/buffer/image/memory。
- 原生descriptor pool reset用generation区分旧set，拒绝重置仍在途的set，失效旧命令和句柄；
  不对原生reset已释放的set再次free。重置后可重新分配/录制/执行。
- 原生pipeline cache初始数据、size/data/INCOMPLETE语义与merge；缓存字节复制到guest，
  不把guest指针或driver对象直接传入宿主。与原API相同，外部同步仍由调用方负责。
- 可变格式/extended-usage 2D图片、显式分量swizzle、单层array view；image-format-list
  仅为兼容性元数据，列出一个格式不表示已支持创建该格式。实际image/view仍经profile过滤。
  新增RGBA8 SNORM/UINT/SINT图片，native blend/storage/texel格式能力按已实现路径过滤。
- 九种核心dynamic graphics states及原生命令、discard/depth-bias和point/line/triangle拓扑。
  当前line width为1、depth-bias clamp为0，未实现的静态bias参数会明确失败。
- 真实`KHR_portability_subset`查询（仅宿主有该扩展时）让原版Mesa选择MoltenVK上游路径；
  只返回本桥实际支持且原生支持的子集。triangleFans、stride、swizzle、sampler bias等
  受真实限制约束，不让不支持的图形状态进入driver。

## 原版Mesa进度与下一批

Alpine签名Mesa25.1.9-r0和loader保持原样；五项硬扩展已满足。离屏客户端完成64个清屏像素、
64个uniform GLSL像素及三轮共192个纹理采样像素校验，并正确销毁context/资源。
后续继续扩展格式/图像维度/mipmap、渲染状态和真实可用features，打通Wayland EGL窗口及生命周期。
当前仍有custom-border-color、line-rasterization和depth-clip-enable相关上游警告；基础样例通过
不代表所有应用/裁剪/线段状态已可用。未通过的能力不会仅靠枚举名称来启用。
DRI/dmabuf、external-memory FD还需要guest FD与Metal资源身份/生命周期设计；
不把宿主FD或指针直接当guest handle。

里程碑依次为原版Mesa EGL context、GPU clear/readback、着色器绘制、Wayland EGL窗口，
最后验证iPad Metal呈现/resize/同步与窗口生命周期。

## 验证

见[查询/模板基础验证](../validation/zink-foundation.md)、[同步/viewport验证](../validation/zink-sync.md)与[renderpass/imageless验证](../validation/zink-renderpass.md)。测试使用原版Mesa与loader，
不修改Mesa使其跳过硬要求。`zink-foundation.py`明确分开基础检查和`rendering_passed`；
`--require-render`在EGL仍不可用时必须失败。

本地native参考是Mesa25 Lavapipe软件Vulkan。`LIBGL_ALWAYS_SOFTWARE`仅用于让原版
Zink选中这个CPU physical device；它不替换OpenGL实现。iPad GPU验收不使用此变量。
此前同步/renderpass批次的MoltenVK/Metal timeline、imageless附件重绑定、可选深度、GPU读回与清理，
已在CI的Apple Paravirtual device通过。
本批原版GL在Metal上的结果待回填；Wayland EGL呈现与物理iPad仍待验收；本批没有构建新的Zink IPA或release。
