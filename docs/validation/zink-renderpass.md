# Renderpass2 / imageless framebuffer 验证

本批沿main推进，基线`b1494b3`。新增原生driver-backed renderpass2、imageless
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

[本地完整输出、结果和图片摘要](zink-renderpass/)保留；Apple/MoltenVK和Linux CI待回填。
没有构建IPA或发布release。
