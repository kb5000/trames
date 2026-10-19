Trames 1.2.0 / build 33 增加基础 Wayland EGL/GLES2 的 Zink 原生 GPU 窗口链路，并包含近期运行效率优化。

- Linux ARM64 应用通过 Mesa Zink → Vulkan loader → Trames ICD → MoltenVK/Metal 呈现到现有浮窗/标签页。正常呈现直接使用 GPU 图像。
- 保留原版 Gallium/Zink/NIR/GLSL/SPIR-V/GLES2；仅给 Mesa 25.1.9 EGL 增加最小的无 DRM Kopper 平台适配。开启 Vulkan/GPU 后自动安装标准 `libEGL.so.1`，保留 apk 管理的 Mesa 文件。
- Vulkan 增加真实图像复制、查询/描述符/同步与渲染资源支持，目前共172个公开入口；修复交换链采样、调整大小时的复制及 GPU 内存选择问题。
- Linux 与 Apple Metal 的两轮双窗口、调整大小、最小化/还原、关闭、重启及旧帧保活通过；Apple 校验全部导出纹理像素。
- 优化通用目录查找、目录枚举及单线程调度路径，减少 Docker 启动等场景的额外开销。不包含 JIT。

下载 unsigned IPA 后用 Windows Sideloadly 签名安装。沿用相同 Apple ID 和有效 bundle ID 可保留数据。需要图形功能时开启应用里的 Vulkan/GPU 开关；Linux 内按需安装 `mesa-dri-gallium mesa-egl mesa-gles vulkan-loader wayland-libs-egl`。

目前是基础 GLES2 和受限 Vulkan profile，并非完整 OpenGL/Vulkan 一致性认证；更多格式、mipmap、高版本 GL 和渲染状态仍在逐步补齐。Mesa ES2 当前使用上游 `mesa_glthread=false` 配置。保留原有 Docker/容器兼容边界，File Provider 激活仍需正式 App Group 签名。

已通过的源码相同项目直接复用，本版重新编译优化的 ARM64 iPad Release 应用。Apple CI 使用 Paravirtual Metal；本版物理 iPad 运行及性能由后续真机测试确认。提供 GPLv3、上游许可证，以及对应的项目、musl、MoltenVK 和 Mesa EGL 源码/补丁；历史 Release 与 tag 不修改。
