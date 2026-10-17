0.17.0/build 19 加入宿主原生 Wayland 合成器、统一窗口与标签页，以及更完整的 Vulkan 图形支持。

Windows 下载 `Trames-0.17.0-build19-unsigned.ipa`，用 Sideloadly、原 Apple ID 和实际 Bundle ID 覆盖安装可保留数据。最低 iPadOS 17，原 Bundle ID 为 `org.trames.Trames`。

- 原版 Linux Wayland 应用各自显示为 App 内小窗，支持移动、调整大小、最小化、关闭与停靠。调整大小会请求客户端重新生成对应分辨率的图像。终端与图形应用共用顶部标签栏，可拖出为浮窗、拖回停靠或重排；旧“显示窗口”菜单和固定分辨率显示器界面已移除。
- UIKit 原生输入桥接 text-input v1/v3，支持预编辑、中文、emoji、选择和光标状态。修复 Weston editor 切换文本框不取消原激活状态、图形窗口抢占终端键盘，以及空 OSC 标题使 Alpine 标签变空的问题。
- 标准 `VK_KHR_wayland_surface` 把原生 Vulkan 图像直接交给 Metal 合成，常规显示不读回像素。共享 SHM 图像优先直接导入，复制回退单独计数；资源保持到生产者与最终显示完成。支持 FIFO 与 Mailbox 各自的队列和图像复用语义。
- 原版 vkmark 所需的原生采样各向异性、D32 深度、静态／动态视口和裁剪、颜色／alpha 混合及传输清屏已补齐。原版 vkcube、Weston 客户端与 vkmark 无需修改。Vulkan 仍需在设置中开启，重新打开本地终端后生效。
- Linux 总内存与单进程预算可在 App 设置中配置，默认 1 GiB／512 MiB，按需使用。并行内存映射改为增量维护，加入 inotify；真实 Node `fs.watch` 与 code-server 的兼容性验证继续推进，完整 code-server 编辑体验尚未作为本版承诺。
- 解释器加入硬件保护的 RAM 直通映射与单级缓存；特殊映射保留原有处理。修复开启 Vulkan 后第二次启动的 `virtual filesystem errno 17`。继续使用无 JIT 的 ARM64 CPU 解释器；GPU 着色器由原生驱动编译。

这是选定 Wayland／Vulkan 接口的实现，尚非完整一致性支持；剪贴板与拖放、自定义鼠标图像、自动响应式弹窗布局及 OpenGL／EGL 桥接未包含。模拟器的程序化输入与画面验证不等同于真机触控和系统输入法候选体验。

窗口使用见 `docs/guides/wayland.md`，验证范围见 `docs/validation/wayland-workspace-vkmark.md`。本轮原版 vkmark 的短场景仅做功能验收，不作为性能成绩。

GPL-3.0-or-later。固定 tag 提供 Trames 完整源码；Release 配对提供混合 musl、原版 MoltenVK 与固定依赖源码、IPA 校验和及核验报告。已有版本的 tag 和安装包保持不变。
