# Renderpass2 / imageless framebuffer 验证

本批沿main推进，基线`b1494b3`，运行源码为
[`59dccdb`](https://github.com/kb5000/trames/commit/59dccdb96072eeca2dfca1b0afb388a0d242a4fa)。新增原生driver-backed renderpass2、imageless
framebuffer及其依赖查询/创建链；没有修改Mesa、loader、CPU/MMU、Linux、网络或App UI。

本地验证：

- `trames-vulkan` Clippy全部targets通过，17个单元测试通过。
- 同一原版Vulkan应用分别使用legacy、renderpass2、imageless+renderpass2、
  imageless+legacy begin，以及imageless颜色+深度附件。新增四组各三帧，GPU颜色由
  独立重心方程检查；native与guest的全部8张最终PPM逐字节相同。
  Imageless两张独立图片按帧交替绑定。
- 原生`renderpass`回归覆盖feature未启用、缺少附件、区域溢出、legacy/renderpass2、
  可选深度、重复更换附件、真实GPU clear/readback、提交后移除guest资源句柄、pool reset，
  以及各260次renderpass2/framebuffer输出backing失败，未耗尽对象表。
- 原版loader的API1.0应用会追加重复properties2扩展，扩展解析已改为幂等。
- 原版Mesa25.1.9已越过五项硬扩展并创建device，目前停在pipeline layout，返回
  `VK_ERROR_FEATURE_NOT_PRESENT`。`rendering_passed=false`，未产生OpenGL画面。

本地native参考为Mesa25.0.7 Lavapipe/LLVM19软件Vulkan；不是iPad GPU性能结果。
原版Mesa额外报告logicOp、fillModeNonSolid、shaderClipDistance、scalarBlockLayout、
custom-border-color和line-rasterization缺失，保留为下一批的真实能力缺口。

CI已通过：

- [Linux39227761983](https://github.com/kb5000/trames/actions/runs/39227761983)：17项单元
  测试、严格Clippy、原生timeline/renderpass/lifecycle；原版Alpine签名Mesa/loader安装，
  1/4核同步检查和18张native/guest逐字节图像对照。44个case中，43个预期成功项目退出0；
  原版Mesa EGL兼容探针退出1，明确保留`rendering_passed=false`，缺少硬扩展列表为空。
  原生参考为Ubuntu Mesa25.2.8 Lavapipe/LLVM20软件Vulkan。
- [Apple39227762013](https://github.com/kb5000/trames/actions/runs/39227762013)：严格Clippy、
  17项模块测试、iOS FFI编译，以及原版MoltenVK1.4.2/Metal timeline和imageless GPU例程。
  Apple Paravirtual device实际完成legacy/renderpass2、imageless更换附件、可选深度、GPU
  读回、清理及输出失败回滚。renderpass例程关闭后driver报告0 MB GPU内存仍分配。
  CI GPU与iOS编译结果不代替物理iPad验收。
- Linux artifact `12175241894`的SHA-256为
  `31264d7364bb6397af74f0a14c6d851153cd3299d422847e071ed123848aefb2`；
  下载摘要和全部ZIP CRC已核验，元数据、结果、原始输出与18张图片摘要均已归档。

[本地输出](zink-renderpass/) · [Linux原始结果](zink-renderpass/ci-linux/) ·
[Linux检查日志](zink-renderpass/linux-checks.log) · [Apple日志](zink-renderpass/apple.log)。
CPU/MMU、Linux、网络、UI通过记录按未变源码复用；没有构建IPA或发布release。

下一处已识别的布局不兼容：相同版本上游`zink_program.c:976`的
`zink_pipeline_layout_create`使用`VK_SHADER_STAGE_ALL_GRAPHICS`作为push-constant可见性，
当前`shader_stages`校验只允许vertex/fragment/compute位。下一批需区分布局可见性与
实际shader执行能力，并继续补原版Mesa报告的其余真实features；本批没有伪造这些能力。
