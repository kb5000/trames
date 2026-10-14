# 0.14.0/build 15 — 原生并行与访存更新

本版合并 0.13.0 交付后的原生与访存工作，运行时优化为
`02ed2b7529002e5927ae2e47f8483e628cc19b1a`，实际打包源码和固定 tag 为
`29464a8d7ec774ba4d9fb4e51c147c2ebe999340`，版本为 0.14.0/build 15。
沿用 `org.trames.Trames` 和 iPadOS 17 最低版本；同一 Apple ID 与 Sideloadly
实际 Bundle ID 覆盖安装可保留 Linux 数据。磁盘格式没有改动。

- [批量原生访存](bulk-native.md)：驻留 guest memory 借用、ARM64 NEON
  扫描比较，纯函数扩大到 19 项，包括 strcat/strncat。
- [原生工作线程并行](parallel-nativecalls.md)：内存／字符串、整数和浮点
  解析及排序步骤在所属 CPU 运行，支持嵌套 guest 回调。
- [64 字节区域访存](region-memory.md)：允许同页不同区域并行，保留 ARM
  原子、独占访问、acquire/release 和屏障要求。
- [共享 CPU／原生访存与批量分配器传输](native-allocator.md)：基于原始
  musl 源码实现的原生 malloc/free 仍为实验功能，默认关闭；iPad 使用原始
  guest 分配器。
- [稀疏脏页提交](checkpoint-cost.md)：每个共享 VM 每轮提交一次，避免
  全地址空间扫描和重复创建 CPU 解码占位缓存。

本次开发宿主的四核分配负载中位数降低约 14%～18%，每轮内存处理时间
降低约 27%～31%。其他负载仍有回退与样本波动；这不是物理 iPad 跑分，
不承诺整体提速。版本元数据调整后不重跑性能基准。

GPL-3.0-or-later、musl 和 Arm 原始许可证声明保留，构建提供完整混合 musl
与配套源码归档。未使用 JIT，默认虚拟 CPU 数仍取宿主可用核心数。

## 构建与下载

已于 2026-10-14 发布 [GitHub Release v0.14.0](https://github.com/kb5000/trames/releases/tag/v0.14.0)。
Windows 可直接下载 [Trames-0.14.0-build15-unsigned.ipa](https://github.com/kb5000/trames/releases/download/v0.14.0/Trames-0.14.0-build15-unsigned.ipa)，
交给 Sideloadly 签名后覆盖安装。Release 的 IPA 无需解压，GitHub Actions 的
artifact ZIP 才需要解压。同一 Apple ID 与实际 Bundle ID 可保留数据。

- [Apple CI](https://github.com/kb5000/trames/actions/runs/38565242158) 与
  [Linux CI](https://github.com/kb5000/trames/actions/runs/38565242134) 在实际
  打包源码上通过；[自动发布任务](https://github.com/kb5000/trames/actions/runs/38568102228)
  等待两条 CI 成功并核验全部构建产物后发布。
- ARM64 iPhoneOS 设备包，最低 iPadOS 17；ZIP CRC、SHA-256、执行权限、
  Mach-O 平台和版本通过核验。全部 41 个包内文件与设备构建逐字节一致。
- 7 项 iPad 模拟器测试通过，0 失败；包内 libc 与模拟器、混合 musl 产物一致。
  原生函数与配套源码输入均为 19 项，原始源码与许可证已核对。
- 公开 Release IPA 再次下载后与独立核验的 Actions IPA 完全一致。
  完整数据见 [核验记录](release-0.14-data.json) 与
  [模拟器结果](release-0.14-simulator.json)。

IPA SHA-256：

```text
6c4dd9e055639f52405b30f10a16b961a08fba6e97d2b439009e094c2b335681
```

Release 同时提供校验和、核验 JSON、模拟器结果及
`musl-hybrid-1.2.5-source.tar.gz`。完整仓库源码使用固定 `v0.14.0` tag
及 Release 的 Source code ZIP/tar.gz；公开 Release 资产不受 Actions 的
7/14 天保留期限影响。已经发布的 tag 和安装包保持不变。

本安装包仍保留 Issue #9 的间歇性 SSH 关闭竞态；其发布所用真实 OpenSSH
检查通过。自动发布后，main 的 `6596b68` 加入[确定性复现和源码修复](niossh-close.md)，
这项后续改动不属于上述 IPA；后续合并版本再交付。物理 iPad 验收仍由
[Issue #8](https://github.com/kb5000/trames/issues/8) 跟踪。

[0.13.0 交付记录](release-0.13.md)保留原有历史安装包与证据。
