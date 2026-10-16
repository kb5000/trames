# Wayland 开发构建

此功能位于 `feature/native-wayland`，跟踪 [Issue #25](https://github.com/kb5000/trames/issues/25)
与 [PR #26](https://github.com/kb5000/trames/pull/26)。已发布的 0.16.0 不包含它。
原版 ARM64 Linux 客户端连接到 App 内原生合成器，不需要启动 guest Weston。

在包含此功能的 iPad 构建中，SHM 窗口默认可用。Vulkan 应用还需在设置中开启
“Vulkan 计算与图形（实验）”，随后重新打开本地终端。默认 root 会话已经设置
`XDG_RUNTIME_DIR=/run/user/0` 和 `WAYLAND_DISPLAY=wayland-0`。

```sh
apk add wayland-utils weston-clients weston font-dejavu breeze-cursors vulkan-loader vulkan-tools
wayland-info
weston-simple-shm &
weston-editor &
vkcube --wsi wayland
```

`weston` 包在这里用于安装原版客户端装饰 PNG 素材；无需执行 `weston` 命令。
首次安装会下载客户端的原始运行依赖。上述示例使用原版包，无需替换 libwayland
或修改应用源码。Vulkan 使用标准 loader 与 App 自动安装的实验性 ICD。

每个顶层窗口有独立标题栏，可拖动、调整大小、最小化、关闭或最大化为工作区标签。
拖动右下角会发送新的尺寸请求，客户端确认后重新生成对应分辨率的图像。
最小化窗口从顶部标签恢复；最大化标签与终端标签可切换，guest 资源持续保留。
触摸、指针和硬件键盘发送给聚焦表面；使用 text-input v1/v3 的应用可接收
UIKit 原生输入法的预编辑和提交，Weston editor 是对应验收应用。

普通 SHM 图像使用共享文件映射，由 Metal 直接读取；导入失败时的复制会计入统计。
Vulkan 图像直接交给原生 Metal 合成，没有常规的像素读回与重新上传。
缩放、生命周期、资源上限和当前协议子集见[架构说明](../architecture/wayland.md)。
完整 Wayland/Vulkan 一致性、剪贴板与拖放、自定义鼠标图像、自动响应式弹窗布局
以及 OpenGL/EGL 桥接不在首版范围。具体 Apple 验收结果以 PR/验证记录为准，
iPad 真机上的输入法与触控体验仍需实际使用验证。
