# Zink 路线与当前边界

先合并性能PR #31（main121cdc7），后续在main单线推进。
[Issue #32](https://github.com/kb5000/trames/issues/32)跟踪Zink完整链路。

目标是原版Linux ARM64 OpenGL/EGL应用→原版Mesa Zink/NIR/SPIR-V→原版Vulkan
loader→Trames guest ICD→现有原生Vulkan/MoltenVK→Metal。保留上游算法，不另写一套
OpenGL实现。后续窗口继续使用现有Wayland合成器和App浮窗/标签页。

当前完成了Vulkan查询/descriptor template、maintenance1/timeline，以及renderpass2/imageless framebuffer基础，**原版Mesa25.1.9仍不能初始化
EGL context，尚未产生OpenGL画面**。这不是完整Vulkan或OpenGL一致性声明。

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

## 原版Mesa进度与下一批

选定版本为Alpine签名Mesa25.1.9-r0。相同版本上游列出的maintenance1、renderpass2、
imageless framebuffer、timeline、descriptor template五项硬扩展现在均通过查询，原版Mesa
已创建Vulkan device。本地Lavapipe参考停在`vkCreatePipelineLayout`的
`VK_ERROR_FEATURE_NOT_PRESENT`，仍未创建EGL context或产生OpenGL画面。

原版Mesa同时报告缺少`logicOp`、`fillModeNonSolid`、`shaderClipDistance`、
`scalarBlockLayout`、custom border color和line rasterization。下一批先处理pipeline/
descriptor布局与这些真实features，再扩展图像/格式/视图/mipmap及渲染状态；不能只抹掉
错误或跳过上游要求。MoltenVK工作路径仍需实际EGL创建后验证。
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
本轮MoltenVK/Metal timeline、GPU填充/读回已在CI的Apple Paravirtual device通过。
原版GL/Wayland呈现与物理iPad仍待验收；当前没有新的Zink IPA或release。
