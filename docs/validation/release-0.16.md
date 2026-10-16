# 0.16.0/build 17 — 容器、直连网络与解释器优化

本版正在构建与验收，完成后在 [GitHub Release v0.16.0](https://github.com/kb5000/trames/releases/tag/v0.16.0) 提供 `Trames-0.16.0-build17-unsigned.ipa`。构建来源、哈希与实际 CI／模拟器结果会在发布完成后记录。

完整范围和安装说明见[发行说明](release-0.16-notes.md)。包括 [guest Docker](../guides/guest-docker.md)、[默认直连网络](../architecture/direct-network.md)、[显式尾调用](../architecture/tail-dispatch.md)、[懒标志](lazy-flags.md)和[软 TLB](tail-tlb.md)。近期未显示稳定收益的解释器实验继续归档。

保持 `org.trames.Trames`、iPadOS 17 和 Sideloadly 覆盖安装路径。磁盘 v3 自动升级到 v4，旧版不能读取新格式；先导出重要文件。发布前核验 IPA 与 ARM64 Release 设备构建逐字节相符、SHA-256、平台、执行权限、原始源码与许可证。

历史 [v0.15.0](release-0.15.md) 与其安装包保持不变。
