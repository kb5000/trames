# 原版 Firefox 兼容性探测

2026-10-20，运行时源码 `499e44d4d5d5a2b6f6b573960adcd38c2792a1ba`。
**目前不能宣称 Firefox 可用**：版本查询通过，Wayland 默认启动有一个已定位的
socket ABI 缺口，无界面页面测试仍未完成。此次只增加探测工具和记录，没有修改
生产运行时、降低产品沙箱要求或构建 IPA。后续跟踪：[Issue #33](https://github.com/kb5000/trames/issues/33)。

## 环境与验收条件

- 原版 Alpine v3.22 aarch64 `firefox-142.0-r0`，固定 minirootfs 3.22.6，
  原签名仓库、`font-dejavu` 与 GTK 依赖。没有修改 Firefox 或 libxul。
- Linux x86_64 测试宿主、4 个可用 CPU，Release 生产 App FFI，当前 hybrid musl、
  pure accelerator，4 GiB 总提交内存/单地址空间上限；不是 iPad 实测。
- Vulkan/EGL 组使用产品 guest ICD 与原生 WSI 的 Mesa EGL 适配库，
  不代表已验证 Firefox 使用了 GPU。确切二进制哈希见 [manifest](firefox/manifest.json)。
- 关闭外部网络，读取本地 HTML：JavaScript 更新标题为
  `TRAMES_FIREFOX_JS_OK`，并在 canvas 上绘制绿色矩形。
- 默认不设置任何 `MOZ_DISABLE_*SANDBOX`。这里的“默认沙箱”描述启动配置，
  尚未证明 Firefox 内容进程的 namespace/seccomp 等限制已经正确生效。
- 无界面通过条件：Firefox 返回 0 且能导出新生成的 PNG。窗口通过条件：
  原生合成器收到帧且窗口标题带 JS 标记。窗口连接、版本输出都不等同于通过。

## 实测结果

| 测试 | 结果 | 解释 |
| --- | --- | --- |
| 原版包安装与 `firefox --version` | 通过，输出 `Mozilla Firefox 142.0` | 不代表浏览器主引擎、内容进程或图形可用 |
| 无界面、默认沙箱，初始无 GPU 组 | 180 秒无截图，进程仍在 | 进程表仍列出 `glxtest`；单凭该列表不能判断子进程仍在执行 |
| 无界面、产品 GPU/EGL，默认沙箱 | 60 秒无截图 | 增加宿主 PC 采样，时间不用于性能比较；没有新截图 |
| Wayland、产品 GPU/EGL，默认配置 | 约 2.2 秒退出 1，零窗口/帧 | Unix socket 的 `FIONREAD` 返回 `ENOTTY`，直接中断 Firefox 的 Wayland 代理 |
| Wayland，仅诊断性设置 `MOZ_DISABLE_WAYLAND_PROXY=1` | 协议握手成功，90 秒仍无窗口 | 已收到 registry、seat 与 sync 回应；绕过首个阻塞并不足以启动浏览器 |
| 无界面，仅诊断性设置 `MOZ_AVOID_OPENGL_ALTOGETHER=1` | 60 秒仍无截图 | 不能把停顿直接归因于 EGL 实现；补充状态探测确认 `glxtest` 是已退出的 Z 状态 |
| 独立执行原版 `glxtest`，设置上述跳过 GL 变量 | 约 0.31 秒返回 1，输出预期的主动跳过信息 | 证明这条独立执行路径能结束；不是 GPU 探测成功 |

各组使用独立 Firefox profile，在同一隔离测试 store 中顺序执行。没有做
禁用内容沙箱的试验，也没有访问网站、检验 WebGL 或验证日常浏览性能。
原始日志和每组命令见 [证据目录](firefox/)。另有 8 秒的 CLI 跟踪/非跟踪
短时对照及 15 秒前台跟踪；其 shell 的退出 0 只表示诊断命令完成，绝不是
Firefox 页面成功。全量跟踪只保留 SHA256 与关键片段，避免提交大量重复调用。

## 已确认的首个阻塞：socket FIONREAD

默认 Wayland 启动的系统调用跟踪：

```text
trames pid 8 syscall 29(0xc, 0x541b, 0x3cc16ce4) = -25
Warning: ProxiedConnection::TransferOrQueue() broken source socket
 : Not a tty
Error: ProxiedConnection::Process(): Failed to read data from client! : Not a tty
```

`29` 是 ARM64 `ioctl`，`0x541b` 是 `FIONREAD`，`25` 是 `ENOTTY`。
Firefox 142 对应的 [原始代理源码](https://hg.mozilla.org/releases/mozilla-release/file/FIREFOX_142_0_RELEASE/third_party/wayland-proxy/wayland-proxy.cpp#l420)
在 `poll(POLLIN)` 后查询源 socket 的可读字节数；查询失败便销毁连接。
因此这条错误因果明确，发生在窗口创建之前。

[最小探针](../../tests/fixtures/firefox-socket-probe.c) 对比宿主 Linux 和 guest，
检查空队列、写入 7 字节、通过 dup FD 读走 3 字节、读空后关闭对端四种情况：

- 宿主 Unix stream 与 pipe 都返回 `0, 7, 4, 0`，8 项全部通过。
- guest 8 项均返回 `ENOTTY`。当前 ioctl 仅处理 inotify 的可读字节数，
  socket ioctl 的白名单也没有这一请求。

修复应覆盖普通 Unix stream、连接到宿主 Wayland 的桥接 socket，以及 pipe，
查询不能消费数据或破坏非阻塞/dup/关闭语义。datagram 的语义需独立按 Linux
验证，不能把总队列长度直接用于所有 socket。不能用恒定返回 0 替代，
否则代理将不再转发已经就绪的消息。最小探针只测 stream/pipe，未覆盖整个要求。

## 其他观察与后续顺序

1. 先补齐上述 `FIONREAD`，再重测默认 Wayland 路径。诊断环境变量不写进产品默认值。
2. 定位首次启动进展：跟踪 Firefox 主进程及线程的 PC、FD、读写和等待，
   区分初始化计算、真正阻塞与调度问题。补充 20 秒生产 FFI 状态探测中，
   `glxtest` 为 Z 状态，说明它已退出；CLI 系统调用跟踪也观察到其 `exit_group(1)`
   和后续线程创建。因此不能把进程表中保留的名字当作 GPU 探测死锁，也不能仅凭
   跟踪/不跟踪两组的进度差异判定异步服务回退。尚未确定无截图的根因。
3. 达到本地页面的 JS 标题与首帧后，再逐项验证内容进程沙箱、共享内存和
   SCM_RIGHTS、线程/信号/定时器、软件渲染与 EGL/GLES/WebRender 路径。
   这些是待验收项，不能根据此次早期失败直接列为“全部未实现”。
4. 最后测试真实网页、HTTPS、字体、输入法、剪贴板、音视频和 iPad 内存峰值。
   Firefox 本身使用 OpenGL/EGL/WebRender 等路径，并非接通 Vulkan 就能自动运行。

`dmesg` 还记录 `comm="Netlink Monitor"` 的 `bind` 返回 `EOPNOTSUPP`。
它涉及网络监视，应继续补全；目前没有证据说明它是页面启动停顿的原因。

安装准备第一次漏收了 GTK `install_if` 引入的 `at-spi2-core` 和 `libxtst`，
补齐原版签名 APK 后安装成功。这是离线输入收集问题，准备脚本已显式收集；
不是 Firefox 的运行时缺口。清理大量仓库文件时还观察到 `rm -rf` 留下文件并
报目录非空，尚未独立定位，不能把它算成已修复问题。准备工具按已知输入路径
删除临时 APK，以便重复使用测试 store。

## 复现

先准备当前 Release `trames-run`、`libtrames_ffi.so` 和
`scripts/build-musl-hybrid.sh` 的产物。GPU 组还需当前
`target/vulkan-guest/libtrames_vulkan_icd.so` 与 `libEGL.so.1`。
仓库输入采集需要宿主网络，guest 安装与页面探测不需要外部网络。

```sh
python3 tests/integration/firefox-prepare.py --output target/firefox-fresh
python3 tests/integration/firefox.py \
  --store target/firefox-fresh/store --output target/firefox-wayland \
  --stage wayland --gpu --wayland-debug --timeout 90
python3 tests/integration/firefox.py \
  --store target/firefox-fresh/store --output target/firefox-headless \
  --stage headless --gpu --timeout 180
```

`firefox.py` 默认 FFI 路径为 `target/perf-runtime/release/libtrames_ffi.so`；
其他构建目录用 `--ffi` 显式指定。输出目录必须不存在，禁止同时打开同一个
store。输入版本随缓存的原版仓库索引决定，`inputs.json` 记录全部 SHA256；
此次 142.0 的确切输入见 [输入清单](firefox/inputs.json)。

需要隔离后续问题时可增加 `--disable-wayland-proxy` 或 `--avoid-opengl`，
结果会显式标为诊断。工具也保留 `--disable-content-sandbox`，此次没有使用，
任何这种结果都不能作为默认沙箱可用的依据。失败/超时返回非零，是探测结果，
不是已经通过的 Firefox CI 验收。宿主最小 socket 探针不需要浏览器或图形驱动：

```sh
cc -O2 -Wall -Wextra -Werror tests/fixtures/firefox-socket-probe.c -o /tmp/socket-probe
/tmp/socket-probe
aarch64-linux-gnu-gcc -static -O2 tests/fixtures/firefox-socket-probe.c -o /tmp/socket-probe-arm64
target/release/trames-run --memory /tmp/socket-probe-arm64
```

最后一条当前应输出 8 项 `errno=25` 并返回 1；修复后应像 Linux 对照一样全部通过。
传给 CLI 的 ELF 路径使用绝对路径，避免把额外的 `/proc/self/exe` 相对路径问题
混进这项 ioctl 诊断。
