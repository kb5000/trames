# 0.10.0/build 11 — complete hybrid musl and NativeCall

软件交付验证：2026-10-13（北京时间）。这次合并完整 musl 接入和前一轮 Linux
兼容性补充，作为一次 iPad 更新。Bundle ID 为 `org.trames.Trames`。

## 下载与安装

[下载 Trames-unsigned-sideload](https://github.com/kb5000/trames/actions/runs/38273859458/artifacts/11758852727)，
解压取得 `Trames-unsigned.ipa`，继续在 Windows 上使用 Sideloadly 签名安装。
安装包元数据已验证为 0.10.0/build 11、ARM64 iPhoneOS 设备应用。
既有 Linux 环境启动时更新标准路径的库，保留用户文件，不需要清空数据。

GitHub artifact 保留到 2026-10-27 00:15 左右（北京时间）。之后可通过本记录的
运行时代码和工作流重新构建。

IPA SHA-256：

```text
03b79c06237af6ff19f867d285b9d2ce49aa126fff6e71240eed7b9476123f64
```

## 本次范围

- 从固定版本的原始源码构建完整 musl 1.2.5，保留全部原公开符号和
  `/lib/ld-musl-aarch64.so.1`、`/lib/libc.musl-aarch64.so.1` 路径。
- 五个纯函数通过 NativeCall 使用预编译原生后端；其他 libc、加载器、分配器、TLS
  和线程逻辑继续执行原始 guest 实现。短内存操作和短字符串保留快速 guest 路径。
- iPad 应用捆绑完整库并启用该 profile；CLI 支持 `--musl-libc` 与显式启用/关闭。
  库更新使用已同步的临时 inode 加原子重命名，重复启动时相同内容不重写。
- 合并备用信号栈和 AUTODISARM、membarrier 子集、进程级 ioprio_get、实际
  RLIMIT_FSIZE/SIGXFSZ 约束，以及 dmesg 中的进程名日志。

完整库不代表所有函数均已原生化，Linux 系统调用也仍按已实现子集记录。
当前没有 JIT；普通共享库替换不能自动加速已经静态链接的程序。

## 验证与来源

运行时代码：
[`0fe3193463a515817313e7e35b2b80152b95cc5a`](https://github.com/kb5000/trames/commit/0fe3193463a515817313e7e35b2b80152b95cc5a)，
tree `04701a0057bdbdcbfb02a6ee9dbc73b15e174a34`。

- 195 项单元测试、Clippy、iOS 目标检查通过。
- [Linux CI](https://github.com/kb5000/trames/actions/runs/38273859447) 全部通过；包含
  实际 ARM64 完整库对照、TLS/线程/异常、HTTPS/apk、Vim、Python、GCC/Clang、Git 和 CMake。
- [Apple CI](https://github.com/kb5000/trames/actions/runs/38273859458) 全部通过；包括完整
  guest 库构建、原生 ARM64 测试、设备和模拟器链接、七项模拟器测试（零失败）。
- 下载后核验 IPA 的版本、设备 Mach-O、捆绑 ELF 哈希和 GPL/musl/Arm 许可证声明；
  捆绑库与同一工作流的源码包中的 ELF 完全一致；全部 41 个 IPA 文件与实际
  设备构建产物逐字节一致，ZIP CRC、SHA-256 sidecar 和可执行权限均已核验。

Trames 代码继续使用 GPL-3.0-or-later，复用的 musl/Arm 原始版权声明保留。
[完整库和对应源码](https://github.com/kb5000/trames/actions/runs/38273859458/artifacts/11757977500)
包含完整原始源码归档、全部适配与构建文件。
[实际模拟器报告](release-0.10-simulator.json) · [详细证据与哈希](musl-hybrid.json) · [架构](../architecture/musl-hybrid.md) · [测试与基准](musl-hybrid.md)。

单次 ARM64/QEMU 基准：64 KiB 复制约 9.02 倍；32 字节复制 0.93 倍；Alpine sort
约 1.04 倍。指标限定于已记录的测试环境，不能据此推断 iPad 上所有程序的启动提升。
新版本的物理 iPad 验收仍由实际安装后确认。
