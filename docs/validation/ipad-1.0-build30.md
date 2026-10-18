# iPad 1.0.0 build 30

[下载ZIP](https://github.com/kb5000/trames/actions/runs/39135889869/artifacts/12139164057)，
解压 `Trames-unsigned.ipa` 后用Windows Sideloadly安装。Actions需登录，产物保留至
2026-11-01 14:30 UTC。源码 `7700195e4da14b8bf05b3b1644779b3c47ce16cc`。
未合并main、修改正式tag或创建Release。

本地会话启动时自动挂载 `/run` tmpfs（0755）和 `/dev/shm` tmpfs（1777、64MiB
按需额度），先于Wayland、终端init及其他服务。直接启动dockerd即可，多个本地
终端共享挂载；不重复挂载活跃目录，冷启动临时内容清空。[实现和验证](runtime-mounts.md)。
保留build29的MMU宿主VA和route字节序修复。独立CLI仍由init/调用者准备挂载。

Linux两项FFI定向回归、严格Clippy，以及[Apple同两项检查和Release构建](https://github.com/kb5000/trames/actions/runs/39135889869)通过。
验证权限、tmpfs类型/额度、重复准备保留Wayland socket与PID文件、共享namespace、
磁盘冷重开不残留临时内容。只跑受影响检查，未重复未变的Docker、MMU、route、
CPU、UI或GPU完整验收；对应源码/guest产物绑定原通过结果。

当前aarch64-apple-ios FFI release [optimized]和Xcode Release应用重新编译，
实际下载包核验GitHub ZIP digest及IPA CRC/SHA256，主程序与FileProvider均为
iPhoneOS ARM64、1.0.0(30)、App Group一致；unsigned、guest ELF/manifest及此前
库hash一致，未含调试样例。未声称已在签名后的实际iPad上执行本次构建。

IPA为10,961,095字节，SHA256：

```
2930fa31f0daaef89e46ab6ce40e6dc653de90a9330a13df94b64aa52e9a71fc
```

[机器可读记录与源码闭包](ipad-1.0-build30.json)。
