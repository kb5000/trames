# 0.15.0/build 16 — Linux 兼容与 Vulkan 更新

已发布 [GitHub Release v0.15.0](https://github.com/kb5000/trames/releases/tag/v0.15.0)。

**[下载 Trames-0.15.0-build16-unsigned.ipa](https://github.com/kb5000/trames/releases/download/v0.15.0/Trames-0.15.0-build16-unsigned.ipa)**，
Windows 交给 Sideloadly 签名，用原 Apple ID 与实际 Bundle ID 覆盖安装可保留
Linux 数据。沿用 `org.trames.Trames`、iPadOS 17；未修改磁盘格式。

完整变更见[发行说明](release-0.15-notes.md)：包括 [sudo／时区](sudo-timezone.md)、
[匿名 ELF／execveat](fd-exec.md)、[Vulkan 纹理与顶点／索引](vulkan-textures.md)、
[原生显示与浮动窗口](vulkan-display.md)及原版 NIOSSH 关闭修复。

Vulkan 默认关闭，在新本地会话前开启实验开关。Linux 应用使用原版
`vulkan-loader` 和标准 ICD；MoltenVK／Metal 执行 GPU 工作。108 个选定入口
并非完整 Vulkan；深度／混合、多 mip、桌面窗口协议和指针输入仍待扩展。
原版 MoltenVK 的 iOS 模拟器不支持非零 base vertex/instance，基础绘制已通过
实际 Metal 像素验收；非零偏移在 Linux 原生与 guest 验证。物理 iPad 验收和
iPad 性能仍需实际测试；原生 malloc/free 继续默认关闭，未使用 guest CPU JIT。

## 产物核验

IPA 应用源码为 `18f74153732a8010b611f441accfbed7ab337c28`。固定 tag 为
`b77503c1ccff88f8253551c88150dbc0353b4be7`；两者仅相差发布脚本与发布 workflow，
应用和构建源码完全一致，已在发布前逐项验证。
[Apple CI](https://github.com/kb5000/trames/actions/runs/38705917216) 和
[完整 Linux CI](https://github.com/kb5000/trames/actions/runs/38705917179) 均通过；
模拟器 14 passed / 0 failed / 0 skipped。全部 65 个 IPA 文件与 ARM64
设备构建逐字节一致，ZIP CRC、SHA-256、版本、平台、执行权限、原始源码及
许可证通过核验。公开 Release IPA 下载后再次核对，与核验报告完全一致。

IPA：16741917 字节，SHA-256：

```text
c83b44142042c90d277d01d750d6360e03160694e3f30dcf8e1c1d19063c8d67
```

[核验报告](release-0.15-data.json)与[模拟器结果](release-0.15-simulator.json)
保留构建来源和原始报告。Release 同时提供 IPA 校验和、混合 musl 源码及
`vulkan-native-upstream-source.tar.gz`（MoltenVK 与固定依赖源码）。Trames 完整
源码对应固定 `v0.15.0` tag，GPL-3.0-or-later。旧版 v0.14.0 tag 和 IPA 保持不变。
