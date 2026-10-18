# iPad 1.0.0 build 28 测试安装包

2026-10-18。按项目所有者要求编译 unsigned IPA，供 Windows Sideloadly 签名安装。
源码 `0c43e34243c15a391f822ebc410b82039132156f`，运行时 `cff0148`。
没有合并 main、修改正式 tag 或创建新 Release。

[下载 ZIP](https://github.com/kb5000/trames/actions/runs/39130864515/artifacts/12137858269)，
解压后将 `Trames-unsigned.ipa` 交给 Sideloadly。Actions artifact 保留至
2026-11-01 13:21 UTC；需要登录 GitHub。

相比 build27，这次包含[默认 managed containerd/runc 兼容修正](docker-defaults.md)，
以及[MMU 软件页组重复准备、proc maps 重复扫描修复](runtime-costs.md)。
开发机性能对照保留各自环境和采样边界，未声称 iPad 已恢复某个分数。

验证只覆盖本轮改动：

- [Apple 构建](https://github.com/kb5000/trames/actions/runs/39130864515)成功：内存库61项、内存集成4项、proc-map缓存2项；当前 ARM64 iOS FFI release [optimized] 和 Xcode Release 应用重新编译。
- [Linux scope](https://github.com/kb5000/trames/actions/runs/39130864477)成功：复用已记录的本地 Linux 修复验证和源码未变模块的通过证据，不重复原 Docker/CoreMark 计时及完整验收。
- 前端与 guest 编译输入按 git diff、SHA256 和成功 CI 结果绑定，复用模拟器交互和 guest 产物。没有重跑无改动 CPU、网络、UI、GPU 验收。
- 下载产物后核验 GitHub ZIP digest、IPA CRC/附带 SHA256，检查主应用与 FileProvider 均为 iPhoneOS ARM64、版本1.0.0(28)、App Group一致，未签名。
- 原生 CoreMark、调试 Wayland fixture未打入包。guest musl/Vulkan仍是已验证的相同 ELF，hash 与 build27及各自 manifest一致。

IPA SHA256：

```
b11f0ca21e8aff085723a7ad860e0ffd66949c7a33590de29df06891c6d510ff
```

IPA 10,958,365字节；ZIP 10,884,339字节。原生侧主二进制 SHA256 为
`8472fe3b2dfd8ccdcb22a3520c1b63ac62704c574574bd4f35bc802858a8458e`。
包检查和源码闭包见[机器可读记录](ipad-1.0-build28.json)。
设备签名安装、CoreMark/Docker 实际速度与 FileProvider 正式签名能力仍待真机验证。
