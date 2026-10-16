0.16.0/build 17 合并 guest 内容器、宿主直连网络和通用解释器优化，继续使用无 JIT 的 ARM64 执行器。

Windows 下载 `Trames-0.16.0-build17-unsigned.ipa`，使用 Sideloadly 和原 Apple ID、实际 Bundle ID 覆盖安装。最低 iPadOS 17，原 Bundle ID 为 `org.trames.Trames`。

- Guest 内容器：UTS／mount／PID namespace、chroot／pivot_root、bind／tmpfs、选定 capabilities、cgroup v2、设备节点与过滤、signalfd 和 Unix socket／SCM_RIGHTS。原版 Alpine crun 与 Docker → containerd → crun 的导入、create/start、退出码、日志和清理通过限定配置验收。
- 默认网络改为宿主直连 TCP／UDP 和系统 DNS，逻辑 eth0 展示活动宿主地址。普通流量不再经过 10.0.0.1 网关；应用监听实际宿主 socket，端口访问受 iOS 权限、路由和生命周期约束。
- 通用执行优化：借用已解码指令、延迟计算算术 C/V 标志、编译器保证的显式尾调用、缓存访存权限的软 TLB，以及协调线程的本地 COW。使用固定 nightly 工具链，设备 App 与 Rust 核心按 Release 配置构建。
- 保留原有 sudo、时区、匿名 ELF／execveat、Vulkan／Metal 计算、纹理与浮动显示窗口。Vulkan 与实验性原生 malloc/free 的默认开关沿用原配置。

Docker 当前使用 vfs、guest 根网络、共享 IPC/cgroup namespace 和关闭 seccomp 的选定配置，无需 `--privileged`。没有实现独立容器网络、overlayfs 或完整资源控制；pull/build/Compose/rootless 等未完成验收。使用方法见仓库 `docs/guides/guest-docker.md`。冷启动仍较慢。

覆盖安装可保留数据，但磁盘元数据由 v3 自动升级为 v4，以保存扩展属性与新的 inode 类型。旧版本无法读取 v4；升级前请导出重要文件，不要通过覆盖回旧版降级同一磁盘。

本轮未转正的整数块、固定寄存器驻留与固定槽位融合实验仅保留为源码补丁和测量记录，未纳入默认执行路径。性能数据来自共享开发机，不能直接等同于 iPad 提速；物理 iPad、Safari/LAN 访问和前后台行为仍需实际验收。

GPL-3.0-or-later。固定 tag 提供 Trames 完整源码；Release 附带混合 musl、原版 MoltenVK 与依赖源码、校验和及核验报告。既有 Release 不覆盖。
