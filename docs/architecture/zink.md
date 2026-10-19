# Zink 路线与当前边界

先合并性能PR #31（main121cdc7），后续在main单线推进。
[Issue #32](https://github.com/kb5000/trames/issues/32)跟踪Zink完整链路。

目标是原版Linux ARM64 OpenGL/EGL应用→原版Mesa Zink/NIR/SPIR-V→原版Vulkan
loader→Trames guest ICD→现有原生Vulkan/MoltenVK→Metal。保留上游算法，不另写一套
OpenGL实现。后续窗口继续使用现有Wayland合成器和App浮窗/标签页。

当前完成的是Vulkan查询/descriptor template基础，**原版Mesa25.1.9仍不能初始化
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
- App自带ICD JSON同步为实例1.1；旧JSON原子刷新，保持未变库inode与用户数据。

## 原版Mesa硬依赖与下一批

选定版本为Alpine签名Mesa25.1.9-r0，依据相同版本上游`zink_device_info.py`与
`zink_screen.c`，当前缺少的硬依赖是：

1. `VK_KHR_maintenance1`：负viewport高度、TrimCommandPool与真实宿主启用。
2. `VK_KHR_create_renderpass2`与`VK_KHR_imageless_framebuffer`：保留attachment、
   recording和in-flight依赖，不能只接受创建flags。
3. `VK_KHR_timeline_semaphore`：counter/signal/wait、submit值和guest阻塞调度，
   委托原生driver执行同步。

然后补MoltenVK driver-properties查询/上游workaround条件、图像/格式/视图/mipmap/
渲染状态与scalar-block-layout等基础要求。扩展逐项真实实现后才加入枚举。
DRI/dmabuf、external-memory FD还需要guest FD与Metal资源身份/生命周期设计；
不把宿主FD或指针直接当guest handle。

里程碑依次为原版Mesa EGL context、GPU clear/readback、着色器绘制、Wayland EGL窗口，
最后验证iPad Metal呈现/resize/同步与窗口生命周期。

## 验证

见[查询/模板基础验证](../validation/zink-foundation.md)。测试使用原版Mesa与loader，
不修改Mesa使其跳过硬要求。`zink-foundation.py`明确分开基础检查和`rendering_passed`；
`--require-render`在EGL仍不可用时必须失败。

本地native参考是Mesa25 Lavapipe软件Vulkan。`LIBGL_ALWAYS_SOFTWARE`仅用于让原版
Zink选中这个CPU physical device；它不替换OpenGL实现。iPad GPU验收不使用此变量。
当前没有新的Zink IPA或release，Apple Metal上的新增接口/GL呈现尚待验收。
