# 0.17.0/build 19 — 原生 Wayland、统一工作区与 Vulkan 图形

发布状态、IPA 和原始核验报告见
[GitHub Release v0.17.0](https://github.com/kb5000/trames/releases/tag/v0.17.0)。
[发行说明](release-0.17-notes.md)包含本轮功能及已知范围，
[工作区、输入和 vkmark 验证记录](wayland-workspace-vkmark.md)说明运行验收。

Windows 使用 Sideloadly、原 Apple ID 和实际 Bundle ID 覆盖安装可保留数据。
原 Bundle ID 仍为 `org.trames.Trames`，最低 iPadOS 17。
正式发布流程要求同一应用源码的 Apple 与 Linux CI 通过，核对 IPA 与
ARM64 Release 设备构建，并提供 SHA-256、原始源码和许可证材料。
