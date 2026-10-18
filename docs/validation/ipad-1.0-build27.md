# iPad 测试包 1.0.0 build 27

[下载 Actions ZIP](https://github.com/kb5000/trames/actions/runs/39109373627/artifacts/12129517629)，解压后将 `Trames-unsigned.ipa` 交给 Windows Sideloadly 签名。产物保留到 2026-11-01；不是新的正式 release。

本包包含 guest Docker 的 namespace/seccomp/device 隔离、默认 bridge/firewall/NAT、DNS/TCP/UDP/端口代理，以及 overlayfs/overlay2 写时复制存储。原版 Docker 包和配置仍按[使用说明](../guides/guest-docker.md)安装。正式 v1.0.0 build 26 不包含这些改动。

- 实际构建源码：`33541e7cdb9105bfeb85b53fca57a20a8970efe6`；运行时与已验证的 `31abd3538994e501cb249601c2adcca2b8bc608a` 完全相同。
- [Apple 构建](https://github.com/kb5000/trames/actions/runs/39109373627)成功：Swift parse/typecheck，当前 ARM64 Release FFI 和真机 app 编译、静态链接、打包。主 app 和 File Provider 均为 iPhoneOS ARM64、1.0.0 (27)，共享组信息一致且未签名。
- 只复用源码未变的 Linux/Apple 运行时、原模拟器交互验收和 guest musl/Vulkan 产物；脚本核对源码差异、哈希绑定计划、成功 job、仓库归属和产物有效期。没有重跑 CPU、Linux、Docker、UI、GPU 的已通过套件，也没有复用旧的 app 可执行文件。
- 已下载核对 Actions ZIP digest、内部 IPA SHA256/CRC、两个 Mach-O 的 ARM64/iOS 平台、版本及共享组、两个 guest ELF 的架构/manifest hash。Release 包不含 DEBUG Wayland 验收目录。
- IPA 大小：10,947,245 bytes；SHA256：`247789aa752d63fab0f4e6399a55b587aeecb3a299098aa56fa4087d524d9666`。

原有[网络验证](docker-network.md)与[overlay2 验证](overlayfs.md)保持各自范围；本次不声称 iPad 上的 Docker 已实测。File Provider 正式 App Group 签名边界保持原状。main 与正式 v1.0.0/tag 未改动。

机器记录、源码/产物指纹及原库源码包见 [JSON](ipad-1.0-build27.json)。
