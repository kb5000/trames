Trames 1.3.0（build 35），面向 iPad 的正式更新。

- 修复大块虚拟内存预留挤走普通堆映射、拖慢内存释放的问题。
- 增加硬 MMU 稀疏地址别名容量，减少多进程场景的软件回退；虚拟预留不预分配物理 RAM。
- 完善原生 Vulkan/GLES3、Transform Feedback、窗口重绘和 resize 边界，包含当前 Xwayland GPU、剪贴板及输入适配。
- 复用源码未变的通过结果；直接发布本次已编译构建，不重复运行整套验收。

Firefox：同配置低刷新率诊断中，旧版关闭约 98 秒后被自身 watchdog 终止，新版约 34 秒正常退出；使用包内 musl 的对齐检查约 39 秒正常退出。默认刷新率在 Linux CPU Vulkan 参考环境仍有重绘/交互拥塞，[Issue #33](https://github.com/kb5000/trames/issues/33) 保持开放。本版本尚未完成物理 iPad 上的 Firefox 验收，未内置诊断偏好或原始诊断数据。

下载 `Trames-1.3.0-build35-unsigned.ipa`，在 Windows 上使用 Sideloadly 签名安装。File Provider 的 App Group 能力仍需正式签名环境验收。

项目使用 GPLv3；本 Release 同时提供项目源码及 musl、Vulkan 原生依赖、Mesa、Xwayland 的对应源码和校验清单。
