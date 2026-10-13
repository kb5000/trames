# 0.11.0/build 12 — accelerator, interpreter memory and Node.js

这次把 0.10.0 之后的原生调用扩展、访存优化和 Linux/Node.js 兼容修复合并为
一次 iPad 更新。版本为 0.11.0，构建号为 12；Bundle ID 保持 `org.trames.Trames`。
Windows 继续使用 Sideloadly 签名。软件交付验证日期：2026-10-13（UTC）。

## 下载与安装

[下载 Trames-unsigned-sideload](https://github.com/kb5000/trames/actions/runs/38350719750/artifacts/11791381436)，
在 Windows 上解压取得 `Trames-unsigned.ipa`，交给 Sideloadly 签名并覆盖安装。
继续使用相同 Apple ID 和实际 Sideloadly Bundle ID，可保留已有 Linux 数据；
本次没有磁盘格式变更，不需要清数据。应用最低要求 iPadOS 17，无需激活 JIT。

安装包为 ARM64 iPhoneOS 设备应用，大小 10,018,837 字节。下载链接保留到
2026-10-27 05:34 UTC；到期后可从下述固定源码和仓库工作流重新构建。
IPA SHA-256（解压后的 IPA，非 artifact ZIP）：

```text
f8446e298b234854ac7ec64a8d12e7713a61e6f59c820a39681496a915aad4b3
```

## 累计改动

- 完整标准路径 musl 接入 17 个纯函数，复用原始实现与版权声明；六个整数转换和
  strtod/strtof 等解析优先走预编译原生代码，其他函数保留完整 guest 实现。
- qsort/qsort_r 支持可恢复的 guest 比较器和嵌套调用；实验性 bsearch 默认关闭。
  排序接口的原生化不代表所有排序工作负载均已提速。
- 在检查权限、COW、共享和 EOF 后直接访问 guest 页；缓存已验证的 NativeCall
  入口、减少路径字符串分配，并让常用固定大小访存暴露给编译器内联。
- 私有匿名 PROT_NONE 用区间记录预留地址，提交可访问页时才计入内存额度。
  修复 argv/env 连续排布、musl 信号标志和标准流非阻塞兼容性，补充 TCP 保活参数。
- 签名 Node.js/npm 完成默认 V8、process.title、文件/加密/压缩、Worker、子进程、
  HTTP/fetch、离线本地依赖安装、锁文件和持久数据重开；加入 Linux CI。
- 保留单一 Rust 缓存解释器，无宿主 JIT。V8 生成的 guest AArch64 代码仍由解释器执行。

完整库仍使用 `/lib/ld-musl-aarch64.so.1` 和 `/lib/libc.musl-aarch64.so.1`，
公开符号保持完整。静态链接程序保留自己的 libc。本次不修改磁盘格式，不需要清数据；
使用相同 Apple ID 和实际 Sideloadly Bundle ID 覆盖安装，避免卸载已有应用。

## 验证与速度

最终运行时代码为 [beae605](https://github.com/kb5000/trames/commit/beae605963618b197c199b2512ab16a5706f5dc1)，
tree `f66660b17d4d96acd19cff1f8a4f7dcbdbb3d749`。
[Linux CI](https://github.com/kb5000/trames/actions/runs/38350719926) 与
[Apple CI](https://github.com/kb5000/trames/actions/runs/38350719750) 均针对该源码且首次通过；
包含默认 V8/Node/npm、Vim、Python、GCC/Clang、Git、CMake 和持久存储重开。
实际下载的模拟器报告为 7 passed / 0 failed。
IPA 版本、设备 Mach-O、可执行权限、ZIP CRC 和 SHA-256 sidecar 已核验；
全部 41 个 IPA 文件与设备构建逐字节一致。捆绑 libc 与模拟器库、源码包中的 ELF
完全相同，17 项适配/构建输入与固定源码及打包源码逐项匹配，许可证声明齐全。
完整 CI 结果见 [机器可读记录](release-0.11-ci.json)；
[Node/npm 与开发工具原始日志](https://github.com/kb5000/trames/actions/runs/38350719926/artifacts/11791651690)
保留实际包版本与完整工作流结果。

本地最终通过 219 项 Rust 测试、43 项 ARM64 内存/原生加速测试、指令和 mmap/信号/网络回归。
原始 musl 与 hybrid musl 的全新 Node/npm 存储均通过；实际 ARM64 Node 的标题、SHA256
和 Worker 通信/退出也通过，无 NativeCall fault/rejection。

一组最终代码的 ARM64/QEMU 前后对比中，访存循环约 1.280×，Vim 启动中位数
约 1.072×。Python 中位数约 1.098×，但样本范围重叠，收益不稳定。
这些结果不代表物理 iPad 上的速度，也不代表所有程序的普遍提升。
[详细验证与原始跑分](nodejs-memory.md)。性能样本来自功能实现
[49ad262](https://github.com/kb5000/trames/commit/49ad26209ae7b86cad01e6cfd2faaebbbcaeda30)；
最终版本只增加版本元数据和预留区间元数据耗尽时的 munmap ENOMEM 传播及
保留状态回归，未改动已测热路径，也未为该 errno 修改重复跑分。

仍有选定 Linux ABI 和 CPU 扩展缺口；Node native addons 和通用 demand paging
不属于已验证范围。虚拟 TCP KEEPINTVL/KEEPCNT 保留为兼容参数，未实现独立的 Linux
探测/到期策略。真机验收继续在 [#8](https://github.com/kb5000/trames/issues/8)，
偶发 NIOSSH 问题在 [#9](https://github.com/kb5000/trames/issues/9)，
本版交付在 [#10](https://github.com/kb5000/trames/issues/10) 跟踪。

项目保持 GPL-3.0-or-later；musl/Arm 原始声明及完整对应源码随构建产物提供。
[完整 musl 库和对应源码包](https://github.com/kb5000/trames/actions/runs/38350719750/artifacts/11791082430)
保留到 2026-10-27 05:19 UTC；[固定版本仓库源码](https://github.com/kb5000/trames/archive/beae605963618b197c199b2512ab16a5706f5dc1.tar.gz)
及其中固定上游归档校验和与构建脚本可用于重建。
[IPA/源码核验与哈希](release-0.11.json) · [实际模拟器报告](release-0.11-simulator.json) ·
[Node.js 与内存架构](../architecture/nodejs-memory.md)。

已知失败记录：此前 75390f7 的 Apple job 115138139877 在原有 NIOSSH
窗口调整状态检查处退出，发生在应用/内核测试之前；失败 job 在源码不变的情况下
仅重试一次后通过。最终 beae605 的原生 OpenSSH PTY/SFTP 检查首次通过，但 SSH 代码
未修改；#9 继续记录该问题，本版不宣称修复 SSH 偶发崩溃。
