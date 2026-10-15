# 0.15.0/build 16 — Linux 兼容与 Vulkan 更新

本版按用户要求打包当前开发成果。版本调整前的 Vulkan 源码为
`2d60ae05d7b6b085572b4a2c3f7cc5e12fdcff56`；本次发行版本为 0.15.0/build 16。
完整变更见[发行说明](release-0.15-notes.md)、[纹理验证](vulkan-textures.md)、
[原生显示](vulkan-display.md)、[sudo／时区](sudo-timezone.md)及
[匿名 ELF](fd-exec.md)。

正在构建并核验设备 IPA。只有同一源码的 Linux 与 Apple CI 均成功、模拟器
全部通过、设备产物及配套源码核验通过后，自动任务才发布新 Release。
沿用 `org.trames.Trames`、iPadOS 17；未修改存储格式，使用原 Apple ID 与
Sideloadly 实际 Bundle ID 覆盖安装可保留数据。Vulkan 默认关闭，物理 iPad
验收待后续实际使用。旧版 v0.14.0 tag 和 IPA 保持不变。
