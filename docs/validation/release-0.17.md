# 0.17.0/build 19 — 原生 Wayland、统一工作区与 Vulkan 图形

已发布 [GitHub Release v0.17.0](https://github.com/kb5000/trames/releases/tag/v0.17.0)。

**[下载 Trames-0.17.0-build19-unsigned.ipa](https://github.com/kb5000/trames/releases/download/v0.17.0/Trames-0.17.0-build19-unsigned.ipa)**，Windows 交给 Sideloadly，沿用原 Apple ID 和实际 Bundle ID 覆盖安装可保留数据。原 Bundle ID 为 `org.trames.Trames`，最低 iPadOS 17。

本轮功能见[发行说明](release-0.17-notes.md)：原生 Wayland、与 Alpine 平行的标签页和浮窗、真实分辨率调整、输入焦点与标题修复，以及 vkmark 所需的 Vulkan 功能。旧“显示窗口”菜单与固定显示器界面已移除。Vulkan 默认关闭，开启后重新打开本地终端；普通 SHM Wayland 窗口不要求开启 Vulkan。使用见[Wayland 指南](../guides/wayland.md)。

## 核验

应用源码：`a3451367fdd9d2936ed0a8ee0ee8f4f9b331e8a3`；固定 tag：`d6113f61d5c9f22fe014c414fd58e5cf2ea34f35`。两者仅相差发布 workflow，应用与构建输入一致。[PR #26](https://github.com/kb5000/trames/pull/26) 已合入 main。

[Apple CI](https://github.com/kb5000/trames/actions/runs/39001346219) 和[完整 Linux CI](https://github.com/kb5000/trames/actions/runs/39001346269) 均通过，[发布任务](https://github.com/kb5000/trames/actions/runs/39002745563) 成功。实际模拟器 **33 passed / 0 failed / 0 skipped**：一项窗口拖出／停靠用例，加上 29 项应用测试和三项其他 UI 用例。原版 Weston 输入、vkcube 与 vkmark 到达原生 Metal；新增各向异性、传输清屏与静态 alpha 混合像素用例通过。

IPA 的全部 66 个文件与 ARM64 Release 设备构建逐字节一致，108 个调试 Wayland 输入文件已排除。设备平台、版本、执行权限、ZIP CRC、原始源码及许可证通过核验。发布后从公开链接重新下载全部七个资产，SHA-256 与发布报告及 GitHub 摘要一致；重新检查了 IPA 的设备 Mach-O、版本和原版 Wayland notices。

IPA：14,164,730 字节，SHA-256：

```text
c2cbb3eed53209fc911148852afa642feebb2c43498fada6e90edf4f268fc3a2
```

[完整核验报告](release-0.17-data.json)、[模拟器原始报告](release-0.17-simulator.json)与[手势原始报告](release-0.17-workspace.json)保留来源和结果。Release 同时提供混合 musl 与原版 MoltenVK／固定依赖源码；Trames 完整源码对应固定 `v0.17.0` tag，GPL-3.0-or-later。既有版本的 tag 和资产保持不变。

本版仍为选定 Wayland／Vulkan 接口子集。真机触控与系统输入法候选体验仍需实际 iPad 使用验证；模拟器验收不代表该项已完成。详细边界见[工作区验证记录](wayland-workspace-vkmark.md)。
