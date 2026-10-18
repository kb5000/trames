Trames 1.1.0 / build 32 将 guest 内 Docker 支持和最近的兼容性修复合入正式版。

- 默认 `dockerd → containerd → runc`，使用 overlay2；无需外部 containerd、crun、GOMAXPROCS 或专用 nofile 参数。
- guest 网络/IPC/cgroup namespace、veth/bridge、DNS、filter/conntrack/NAT、TCP/UDP 发布和网络隔离；overlay2 延迟复制、whiteout、持久化与重启。
- 修正交互式容器的 devpts inode 和 O_PATH/proc 设备重新打开，原版 `docker run -it --rm` 的终端输入、输出和退出通过验证。
- iPad 会话启动时自动挂载 `/run` 和 `/dev/shm`，先于 Wayland、终端及服务。
- 限制 MMU 可选别名窗口的宿主虚拟地址占用，保留按需物理分配与 guest 4 GiB 配置；修正 `route -n` 地址/掩码字节序。
- 优化 proc maps 重复遍历和 16 KiB 页组的软件回退准备。开发机测量不代表 iPad 性能保证。

解压/下载 unsigned IPA 后用 Sideloadly 签名安装。延续相同 Apple ID 和有效 bundle ID 可保留原有数据。File Provider 真机激活仍需正式 App Group 签名。

这是已验证范围内的 rootful guest Docker；完整 OCI、rootless、资源控制器、CRI/Kubernetes、BuildKit/Compose、全部 Registry/volume/TTY 行为仍在跟踪。交互式验收确认 Docker API 中自动删除容器，日志中的 shim 清理警告仍保留。iPad 实际性能、后台/前台网络和当前修复的真机验收不冒充 CI 结果。

沿用已验证且源码未变的 UI、CPU 差分、GPU 和 guest 库证据；本版另做 Linux/Apple Rust 工作区总检查和当前 ARM64 Release 应用编译。保留 GPLv3、上游许可证、对应 musl/MoltenVK 源码及本版本项目源码归档。历史 Release 和 tag 不修改。
