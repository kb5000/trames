# 0.11.0/build 12 — accelerator, interpreter memory and Node.js

这次把 0.10.0 之后的原生调用扩展、访存优化和 Linux/Node.js 兼容修复合并为
一次 iPad 更新。版本为 0.11.0，构建号为 12；Bundle ID 保持 `org.trames.Trames`。
Windows 继续使用 Sideloadly 签名。版本构建和安装包的精确证据在构建完成后补充。

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

功能实现 [49ad262](https://github.com/kb5000/trames/commit/49ad26209ae7b86cad01e6cfd2faaebbbcaeda30)
的 Linux/Apple CI 首次均通过；实际下载的模拟器报告为 7 passed / 0 failed。
本地最终通过 219 项 Rust 测试、43 项 ARM64 内存/原生加速测试、指令和 mmap/信号/网络回归。
原始 musl 与 hybrid musl 的全新 Node/npm 存储均通过；实际 ARM64 Node 的标题、SHA256
和 Worker 通信/退出也通过，无 NativeCall fault/rejection。

一组最终代码的 ARM64/QEMU 前后对比中，访存循环约 1.280×，Vim 启动中位数
约 1.072×。Python 中位数约 1.098×，但样本范围重叠，收益不稳定。
这些结果不代表物理 iPad 上的速度，也不代表所有程序的普遍提升。
[详细验证与原始跑分](nodejs-memory.md)。版本元数据提交 [75390f7](https://github.com/kb5000/trames/commit/75390f77413cb9e0a5788cb51fc43e8d785167a2)
首先验证版本元数据；最终构建还补充了预留区间元数据耗尽时的 munmap ENOMEM
传播和保留状态回归，已有热路径实现保持一致。

仍有选定 Linux ABI 和 CPU 扩展缺口；Node native addons 和通用 demand paging
不属于已验证范围。虚拟 TCP KEEPINTVL/KEEPCNT 保留为兼容参数，未实现独立的 Linux
探测/到期策略。真机验收继续在 [#8](https://github.com/kb5000/trames/issues/8)，
偶发 NIOSSH 问题在 [#9](https://github.com/kb5000/trames/issues/9)，
本版交付在 [#10](https://github.com/kb5000/trames/issues/10) 跟踪。

项目保持 GPL-3.0-or-later；musl/Arm 原始声明及完整对应源码随构建产物提供。

版本构建的已知失败记录：Apple 首次 job 115138139877 在原有 NIOSSH
窗口调整状态检查处退出，发生在本轮应用/内核测试之前。失败 job 在源码不变的
情况下仅重试一次；#9 继续记录该问题，本版不宣称修复 SSH 偶发崩溃。
