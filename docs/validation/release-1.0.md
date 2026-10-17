# Trames 1.0.0（build 26）

[正式 Release](https://github.com/kb5000/trames/releases/tag/v1.0.0) · [unsigned IPA](https://github.com/kb5000/trames/releases/download/v1.0.0/Trames-1.0.0-build26-unsigned.ipa)

点击 **+ → 本地 Alpine 终端** 每次创建独立的 shell、PTY 和终端视图，最多 32 个。各终端共享同一 Linux 实例的文件、进程、网络、已安装软件及内存预算，工作目录、环境变量、输入输出和尺寸独立。正常 PTY hangup 关闭选中终端，不关闭整个环境；关闭首个终端、全部终端再打开、后台暂停／恢复均已验证。浮动、调整大小、最小化回标签页、停靠和关闭保留。[架构](../architecture/multiple-terminals.md)。

本版本包含此前 iPad 工作区、系统 App 尺寸／真实 PTY、原生 Wayland／Vulkan 窗口、mousepad 右键复制、导航后输入、跨 App 剪贴板与粘贴权限等待连贴、原版 foot、GTK 固定对话框、Files 重复导入等修复。File Provider 的正式 App Group 签名后真机验收继续按已同意计划延期；免费侧载使用 App 内 Files 导入／导出。

本轮验证只运行新增或受影响检查，未重复完整 CPU、网络、容器、GPU、GTK／foot 与窗口基线：

- 原版 Alpine 三终端生产 FFI 回归：[Linux CI](https://github.com/kb5000/trames/actions/runs/39061233876)，包括共享文件／进程、私有 cwd／环境、独立尺寸、Ctrl+C、输出背压、关闭首个终端、退出、重开与取消未启动的终端。
- 实际 iPad 模拟器工作区、SwiftTerm、guest PTY 和系统 UIPasteboard：[Apple CI](https://github.com/kb5000/trames/actions/runs/39062598012)，新增综合测试 **1 passed / 0 failed / 0 skipped**，包含关闭首个视图后 Bridge 工作、共享暂停／恢复和全部关闭后的重开。
- Apple **165 passed**、Linux **166 passed** 的 VFS／core／FFI library 检查及 clippy/fmt 已通过，每平台保留 1 项原有 ignored 测试。来自先前 job 中独立成功的 native 步骤，后续仅验收脚本修改，源码相同，因此复用；旧 job 的其他验收曾失败，不把整个旧 job 写成成功。
- 原有通过基线见 [build 23](ipad-0.18-build23.json)、[build 24](ipad-0.18-build24.json)、[build 25](ipad-0.18-build25.json)。新模拟器检查不声称覆盖物理 iPad 键盘、实际权限弹窗或物理 GPU 呈现。

生产构建源码：`aa4528166c8d0cdb04f3fe540730d9af63ed7f07`。Linux 验收源码仅在测试／发布文件上不同，运行时代码一致。详细 CI、源文件摘要、重试说明及平台限制见 [机器可读记录](release-1.0.json)。合入只复用验证过的源代码和原始设备 bundle；发布验证脚本在 tag 创建前检查 IPA、构建文件、原始源码与许可证，并上传配套 GPL 源码归档，不覆盖既有 tag／资产。

IPA 10,607,498 bytes；SHA-256：`264df460a1b224a7e0570c43d1fa78de745b18ea7a6824c2aeed635e6cfda754`。主 App／扩展均为 ARM64 iPhoneOS、1.0.0／26、unsigned；guest ELF 与 manifest、共享组元数据、内附摘要已核对。Windows／Mac 使用 Sideloadly 签名后安装。
