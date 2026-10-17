# Wayland 开发构建

此功能面向 0.17.0 及后续版本，0.16.0 不包含它。实现跟踪
[Issue #25](https://github.com/kb5000/trames/issues/25) 与
[PR #26](https://github.com/kb5000/trames/pull/26)，安装与发布状态见[发行记录](../validation/release-0.17.md)。
原版 ARM64 Linux 客户端连接到 App 内原生合成器，不需要启动 guest Weston。

在包含此功能的 iPad 构建中，SHM 窗口默认可用。Vulkan 应用还需在设置中开启
“Vulkan 计算与图形（实验）”，随后重新打开本地终端。默认 root 会话已经设置
`XDG_RUNTIME_DIR=/run/user/0` 和 `WAYLAND_DISPLAY=wayland-0`。

```sh
apk add wayland-utils weston-clients weston font-dejavu breeze-cursors vulkan-loader vulkan-tools vkmark
wayland-info
weston-simple-shm &
weston-editor &
vkcube --wsi wayland
vkmark --winsys wayland
```

`weston` 包在这里用于安装原版客户端装饰 PNG 素材；无需执行 `weston` 命令。
首次安装会下载客户端的原始运行依赖。上述示例使用原版包，无需替换 libwayland
或修改应用源码。Vulkan 使用标准 loader 与 App 自动安装的实验性 ICD。

终端和图形应用共用顶部标签栏。浮动窗口标题栏的停靠按钮会把窗口放到与 Alpine
平行的标签页；拖到顶部不会自动最大化。拖动标签到内容区可重新变成浮动窗口，
在顶部左右拖动可调整顺序；终端标签同样支持这些操作。

窗口可独立移动、调整大小、最小化或关闭。右下角保留无图标的拖动热区，
拖动会发送新的尺寸请求，客户端确认后重新生成对应分辨率的图像。
最小化项目回到顶部非活动标签，点击标签恢复。关闭按钮位于各标签标题右侧。
点击 Alpine 标签或终端内容即可把键盘焦点交还终端，图形窗口继续保留。
旧“显示窗口”菜单和固定分辨率显示器已移除，图形应用直接使用这些窗口和标签页。
触摸、指针和硬件键盘发送给聚焦表面；使用 text-input v1/v3 的应用可接收
UIKit 原生输入法的预编辑和提交，Weston editor 是对应验收应用。

0.18.0/build 21 测试包增加 GTK3 所需的标准 data-device 协议，以及 iPad 双向文本
剪贴板。可用 `apk add mousepad imv` 安装更多原版客户端。聚焦图形窗口后，使用
应用的复制／粘贴菜单、硬件键盘快捷键或 UIKit 编辑菜单即可传输文本。
读取由 iPadOS 控制粘贴权限，写入直接执行，不再弹出 Trames 的额外确认框；
关闭设置中的 Native Bridge 会拒绝新的宿主剪贴板请求。当前上限为 16 KiB UTF-8，
图片剪贴板和拖放尚未实现；实际 iPad 权限与应用体验仍待真机验收。

普通 SHM 图像使用共享文件映射，由 Metal 直接读取；导入失败时的复制会计入统计。
Vulkan 图像直接交给原生 Metal 合成，没有常规的像素读回与重新上传。
缩放、生命周期、资源上限和当前协议子集见[架构说明](../architecture/wayland.md)。
完整 Wayland/Vulkan 一致性、拖放、自定义鼠标图像、自动响应式弹窗布局
以及 OpenGL/EGL 桥接不在首版范围。Apple 模拟器已通过 33 项验收，包含窗口
拖出／停靠、Weston 输入、原版 vkcube 与 vkmark 的 Metal 画面，详见
[验证记录](../validation/wayland-workspace-vkmark.md)；
iPad 真机上的输入法与触控体验仍需实际使用验证。
