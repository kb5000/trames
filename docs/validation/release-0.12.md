# 0.12.0/build 13 — multicore iPad update

本版将已经完成软件验证的多核实现打包为一次 Windows/Sideloadly 更新。
版本 0.12.0，构建号 13，Bundle ID 保持 `org.trames.Trames`；最低 iPadOS 17。
交付跟踪：[Issue #12](https://github.com/kb5000/trames/issues/12)。

## 本版内容

- 默认使用宿主报告的可用逻辑 CPU 数，上限 32；多个可运行 guest 线程/进程
  使用持久宿主工作线程并行执行，同一个 Rust 缓存解释器继续使用，无宿主 JIT。
- 现有 LSE/CASP、独占访问和 acquire/release、内存屏障实现并行同步语义。
  普通访存仍允许竞争；采用 ARM 允许结果的较强子集，不复现所有弱内存交错。
- 共享内存/futex、fork/COW、exec/退出、代码缓存失效和线程浮点状态保持一致；
  亲和性、getcpu 与 proc/sys CPU 拓扑一致，绑核线程不会永久饿死普通线程。
- Node.js 的多路 SharedArrayBuffer/Atomics、完整 npm 和开发工具已纳入验证。
  NativeCall、系统调用和 backing 写回初期仍由协调器集中处理。

运行时代码基于 `912b04059866c6d8cc739cf53d9b1ca848d1c6c1`，
最终开发证据位于 `e7ed080ebe08c2617594ef257a58b1953b969334`。
该源码已通过两套完整 CI、230 项宿主测试、155 项 ARM64 测试和实际模拟器 7/0。
本次打包只调整版本元数据，没有新增运行时或磁盘格式改动；不重跑性能基准。
[多核设计、原始测量与已验证边界](multicore.md)。

## 下载与安装

[下载 Trames-unsigned-sideload](https://github.com/kb5000/trames/actions/runs/38373085151/artifacts/11800896022)，
Windows 解压后将 `Trames-unsigned.ipa` 交给 Sideloadly 签名并覆盖安装。
沿用相同 Apple ID 与实际 Sideloadly Bundle ID，可保留原 Linux 数据；
本次不要求清数据或卸载应用。签名及物理 iPad 验收由用户设备完成。

IPA 大小 10,110,935 字节，版本 0.12.0/build 13、Bundle ID `org.trames.Trames`，
ARM64 iPhoneOS 设备平台，最低 iPadOS 17；包内设备列表为 `[1, 2]`，包含 iPad。
链接保留到 2026-10-27 17:15（北京时间）。IPA SHA-256（解压后的 IPA）：

```text
e108a4e37d3927df04459479ea9e65d2030354b162ce63e82034ebfa0dea1902
```

## 实际构建核验

版本源码为 [0a3120d](https://github.com/kb5000/trames/commit/0a3120d1251cbb50ca2ef104c00c981dcabb1625)，
[Apple 构建](https://github.com/kb5000/trames/actions/runs/38373085151)首次成功。
[Linux 完整回归](https://github.com/kb5000/trames/actions/runs/38373085014)也首次成功。
两套工作流均对应上述固定版本源码。设备 Mach-O、可执行权限、ZIP CRC、SHA-256 sidecar
均已检查；41 个 IPA 文件与设备构建逐字节一致。

实际下载的模拟器报告为 7 passed / 0 failed。
捆绑 libc 和 manifest 与完整库 artifact 一致，libc 与模拟器、对应源码包中的 ELF
完全一致；17 项适配/构建输入及固定上游源码归档逐项核对，原始许可证声明保留。
[IPA 与源码机器可读核验](release-0.12.json) ·
[实际模拟器报告](release-0.12-simulator.json) · [完整 CI 结果](release-0.12-ci.json)。
[多核、Node/npm 和开发工具原始日志](https://github.com/kb5000/trames/actions/runs/38373085014/artifacts/11800654774)
归档已下载并核对 CRC、SHA-256 及通过标记，包括绑核公平性、SharedArrayBuffer/Atomics、
离线 npm、GCC/Clang 编译执行与重开存储的数据保留。

已有开发宿主计算批次中，2/4 核相对该批新版单核约快 1.40×/1.67×，
新版单核计算比旧版慢约 9.3%；内存结果和不同批次存在波动。
这些不是物理 iPad 测量，也不证明单线程程序启动或所有应用都会加速。
真机验收在 [#8](https://github.com/kb5000/trames/issues/8)；
原有偶发 NIOSSH 问题继续在 [#9](https://github.com/kb5000/trames/issues/9)，
本版没有修改 SSH 代码。

GPL-3.0-or-later 和 musl/Arm 原始声明保留；
[完整混合 musl 与对应源码](https://github.com/kb5000/trames/actions/runs/38373085151/artifacts/11800355936)
和[本版固定仓库源码](https://github.com/kb5000/trames/archive/0a3120d1251cbb50ca2ef104c00c981dcabb1625.tar.gz)
提供完整构建输入。0.11.0 的旧安装包和证据保持原有历史记录。
