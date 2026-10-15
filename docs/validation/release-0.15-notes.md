0.15.0/build 16 合并 sudo／时区／匿名 ELF 兼容修复，以及原生 Vulkan 计算、图形、纹理与 App 内显示窗口。仍不使用 guest CPU JIT。

Windows 下载 `Trames-0.15.0-build16-unsigned.ipa`，交给 Sideloadly 签名。沿用原 Apple ID 和实际 Bundle ID 覆盖安装可保留 Linux 数据；最低 iPadOS 17，原 Bundle ID 为 `org.trames.Trames`。

- 原版 sudo 的伪终端、密码认证、提权与嵌套运行；同步 iPad 时区规则并保留显式 TZ。
- 修复 `/proc/self/fd/N` 匿名／已删除文件执行，加入 `execveat`、`AT_EMPTY_PATH`、`O_PATH`；Python memfd 与 pass_fds 测试通过。
- 默认关闭的 Vulkan 实验开关：原版 Linux loader → guest ICD → 原版 MoltenVK 1.4.2／Metal。108 个选定入口支持计算、顶点／索引缓冲、纹理上传与采样、图形描述符和 push constants。
- 标准 display/surface/swapchain 直接向 App 内画布提交 GPU 图像；窗口可拖动、调整大小、隐藏和恢复，画布保持 1024×768。终端菜单可重新显示窗口。
- 固定 NIOSSH 源码修复通道关闭期间的窗口调整崩溃。

原版 MoltenVK 的 iOS 模拟器不支持非零 base vertex/instance；基础索引／实例绘制单独验收，非零偏移在 Linux 原生驱动上验证。Vulkan 是选定子集，仍缺少深度／混合、多 mip、桌面窗口协议与指针输入。原生 Metal 证据来自 iPad 模拟器，物理 iPad 验收与性能仍需实际使用确认；本版不宣称整体提速。实验性原生 malloc/free 继续默认关闭。

GPL-3.0-or-later。固定 tag 提供 Trames 完整源码；Release 附带混合 musl 源码、原版 MoltenVK 与其依赖源码归档、校验和及核验报告。历史 Release 和安装包保持不变。
