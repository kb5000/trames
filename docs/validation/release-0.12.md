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

## 安装包准备与验收

正在为本版版本元数据运行云端 Linux/Apple 构建。下载地址、固定源码、
IPA 校验和、实际模拟器报告、完整库和对应源码记录会在构建核验后补齐；
本记录在此之前不代表安装包已交付。

Windows 解压最终 artifact 后，将 `Trames-unsigned.ipa` 交给 Sideloadly 签名。
沿用相同 Apple ID 与实际 Sideloadly Bundle ID 覆盖安装，保留原 Linux 数据；
本次不要求清数据或卸载应用。签名和物理 iPad 验收由用户设备完成。

已有开发宿主计算批次中，2/4 核相对该批新版单核约快 1.40×/1.67×，
新版单核计算比旧版慢约 9.3%；内存结果和不同批次存在波动。
这些不是物理 iPad 测量，也不证明单线程程序启动或所有应用都会加速。
真机验收在 [#8](https://github.com/kb5000/trames/issues/8)；
原有偶发 NIOSSH 问题继续在 [#9](https://github.com/kb5000/trames/issues/9)，
本版没有修改 SSH 代码。

GPL-3.0-or-later 和 musl/Arm 原始声明保留；完整混合 musl、固定上游源码
与构建脚本继续提供对应源码。0.11.0 的旧安装包和证据保持原有历史记录。
