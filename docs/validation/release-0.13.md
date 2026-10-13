# 0.13.0/build 14 — 持续多线程执行更新

本版打包持续 CPU 工作线程及 poll/epoll 超时清理修复，跟踪 [Issue #14](https://github.com/kb5000/trames/issues/14)。
工作线程保留 CPU 状态与内存视图连续执行；系统调用、NativeCall、异常和定时调度点再协调停顿。
内核和 NativeCall 仍集中处理；ARM 并行同步与现有页面事务边界保持不变，无 JIT。

运行时代码来自 `7eebd3de4c93db214e1d45481cb42d0d9c0d626b`；
版本源码为 `5f88d845027ee257603892024e25fa98d0a9fdd4`。
本次仅调整版本元数据，没有磁盘格式改动，不重跑性能基准。
[实现、正确性证据和同机性能测量](continuous-workers.md)保留各批次源码与测量边界；
四核计算耗时在开发宿主同机批次由 1.83 秒降到 1.05 秒，四核/单核加速比为 3.32×。
这不是物理 iPad 跑分，单线程表现仍有批次波动。

## 构建核验

[Apple 构建](https://github.com/kb5000/trames/actions/runs/38390633418)和
[Linux 回归](https://github.com/kb5000/trames/actions/runs/38390633194)对应本版固定源码。两套工作流均首次成功，所有作业通过。
Linux 包含指令差分、完整 musl、ARM64 pthread SMP、Python、Node/npm、开发工具和持久化工作流。
Bundle ID 保持 `org.trames.Trames`，最低 iPadOS 17。
沿用相同 Apple ID 与 Sideloadly 实际 Bundle ID 覆盖安装可保留 Linux 数据。
物理 iPad 验收继续在 [#8](https://github.com/kb5000/trames/issues/8)。
GPL-3.0-or-later 和原始 musl/Arm 许可证声明保留，提供配套源码归档。

## 下载与安装

[下载 Trames-unsigned-sideload](https://github.com/kb5000/trames/actions/runs/38390633418/artifacts/11810826287)，
Windows 解压后，将 `Trames-unsigned.ipa` 交给 Sideloadly 签名并覆盖安装。
不要求清数据或卸载。下载有效期至 2026-10-27 11:49 UTC。

IPA 大小 10,112,808 字节，SHA-256：

```text
58ade7b80ab04f7917fcc9abca13fb002f5a173e32579d3023b69a8cba107559
```

ZIP CRC、SHA-256 sidecar、可执行权限和 ARM64 iPhoneOS Mach-O 平台均已核对。
41 个包内文件与实际设备构建逐字节一致，设备列表 `[1, 2]` 包含 iPad。
实际下载的模拟器报告为 7 passed / 0 failed。捆绑 libc、manifest 与配套库一致，
17 项源码输入及固定上游源码归档逐项核验；原始许可证声明保留。
[安装包核验](release-0.13.json) · [模拟器报告](release-0.13-simulator.json) ·
[完整 CI 结果](release-0.13-ci.json)。

[完整混合 musl 与对应源码](https://github.com/kb5000/trames/actions/runs/38390633418/artifacts/11808582762)和
[本版固定仓库源码](https://github.com/kb5000/trames/archive/5f88d845027ee257603892024e25fa98d0a9fdd4.tar.gz)
提供完整构建输入。[0.12.0 交付记录](release-0.12.md)保持原有历史证据。
