Trames 1.0.0 支持在 iPad 中同时使用多个本地 Alpine 终端和 Wayland 窗口。

- 点击 **+ → 本地 Alpine 终端**，每次新建一个独立 shell／PTY。所有终端共享同一套 Linux 文件、进程、网络和已安装软件，工作目录、环境变量及终端尺寸各自独立。支持浮动、调整大小、最小化回标签页、停靠和关闭；关闭首个终端或全部终端后再打开，Linux 环境继续保留。
- 宿主原生 Wayland 合成器与 Vulkan／Metal 显示，图形窗口和终端共用工作区。包含 GTK／mousepad seat、右键复制、方向键后输入和跨 App 文本剪贴板修复；iPadOS 读取权限等待期间不再积累重复粘贴。
- 系统 App 最大化／还原刷新真实终端和 PTY 尺寸，后台恢复保留 vkcube 偏好位置；原始 SwiftTerm 扩展键盘与系统调整角落留出间距，状态行和两秒 CPU 采样保留。原版 foot 可运行，固定大小对话框和 Files 重复导入已适配。
- 保留只读 File Provider 的 Trames → Alpine → rootfs 结构及多 rootfs 扩展空间。它仍需要正式 App Group 签名；免费侧载可继续使用 App 内 Files 导入／导出，正式签名后的设备验收尚未完成。

下载 `Trames-1.0.0-build26-unsigned.ipa`，在 Windows 或 Mac 上使用 Sideloadly 签名安装。当前包未签名。GPU 功能仍为实验性功能，在设置中启用；具体应用兼容性受已实现的 Vulkan／Wayland 协议与 Linux 系统调用范围限制。

GPL-3.0-or-later。Release 同时提供 IPA 摘要、构建验证记录，以及配套的 musl 和 Vulkan 原始源码归档；完整 Trames 对应源码位于同一 Release tag。
