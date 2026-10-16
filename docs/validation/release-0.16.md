# 0.16.0/build 17 — 容器、直连网络与解释器优化

已发布 [GitHub Release v0.16.0](https://github.com/kb5000/trames/releases/tag/v0.16.0)。

**[下载 Trames-0.16.0-build17-unsigned.ipa](https://github.com/kb5000/trames/releases/download/v0.16.0/Trames-0.16.0-build17-unsigned.ipa)**，Windows 交给 Sideloadly，使用原 Apple ID 与实际 Bundle ID 覆盖安装。沿用 `org.trames.Trames`，最低 iPadOS 17。

磁盘元数据从 v3 自动升级到 v4，覆盖安装可保留数据；旧版无法读取 v4。
升级前请导出重要文件，不要用旧版覆盖降级同一磁盘。

完整范围见[发行说明](release-0.16-notes.md)：[guest Docker](../guides/guest-docker.md)、[默认直连网络](../architecture/direct-network.md)、[显式尾调用](../architecture/tail-dispatch.md)、[懒标志](lazy-flags.md)和[软 TLB](tail-tlb.md)。近期未显示稳定收益的解释器实验继续归档，未纳入默认运行路径。设备 App 与 Rust 核心使用 Release 配置，无 guest CPU JIT。

Docker 目前仅验收 vfs、guest 根网络、共享 IPC/cgroup namespace 和关闭 seccomp 的选定配置；并非完整 Docker 隔离。Vulkan 和实验性原生 malloc/free 开关沿用原配置。物理 iPad 性能、Safari/LAN 与前后台行为仍需实际验收。

## 产物核验

应用源码：`24b9a945c115adea7e6940120d7cab33299dfa4f`；固定 tag：`0b734f51fe2ed91d6ac037e2e3f295d4d3ac12b9`。
两者仅相差发布 workflow，应用和构建源码完全一致。
[Apple CI](https://github.com/kb5000/trames/actions/runs/38884960115) 与[完整 Linux CI](https://github.com/kb5000/trames/actions/runs/38884960056) 均通过。
实际模拟器 **14 passed / 0 failed / 0 skipped**。
全部 63 个 IPA 文件与 ARM64 Release 设备构建逐字节一致。
版本、Mach-O 设备平台、执行权限、ZIP CRC、SHA-256、库文件、原始源码及许可证均通过核验。
公开 Release IPA 下载后再次独立核对通过。

IPA：13457636 字节，SHA-256：

```text
e8c62938ab8135cae6b0df0595cc928cf8bba50cb1500f52040c116fdc1b11e5
```

[核验报告](release-0.16-data.json)与[模拟器原始报告](release-0.16-simulator.json)保留来源和结果。
Release 同时提供 IPA 校验和、混合 musl 源码，以及 MoltenVK 与固定依赖的原始源码归档。
完整 Trames 源码对应固定 `v0.16.0` tag，GPL-3.0-or-later。历史 [v0.15.0](release-0.15.md) 与安装包保持不变。
