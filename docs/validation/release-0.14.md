# 0.14.0/build 15 — 原生并行与访存更新

本版合并 0.13.0 交付后的原生与访存工作，运行时代码截止
`02ed2b7529002e5927ae2e47f8483e628cc19b1a`，版本元数据为 0.14.0/build 15。
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
- [SSH 关闭竞态修复](niossh-close.md)：基于固定原始 NIOSSH 源码修复关闭
  阶段的窗口调整崩溃，继续交付已缓存的最终输出；保留原始许可证。

本次开发宿主的四核分配负载中位数降低约 14%～18%，每轮内存处理时间
降低约 27%～31%。其他负载仍有回退与样本波动；这不是物理 iPad 跑分，
不承诺整体提速。版本元数据调整后不重跑性能基准。

GPL-3.0-or-later、musl 和 Arm 原始许可证声明保留，构建提供完整混合 musl
与配套源码归档。未使用 JIT，默认虚拟 CPU 数仍取宿主可用核心数。

## 构建与下载

正在为本版固定源码执行 Linux 回归、Apple 设备构建和 iPad 模拟器测试。
完成后在此记录未签名 IPA、源码、SHA-256、包内平台与版本核验结果。
Windows 下载并解压后，用 Sideloadly 签名其中的 `Trames-unsigned.ipa`。

[0.13.0 交付记录](release-0.13.md)保留原有历史安装包与证据。
