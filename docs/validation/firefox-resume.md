# Firefox：默认 headless 页面通过，交互窗口仍待查

后续：[交互与全局刷新任务](firefox-interaction.md)拆分两个刷新来源，并增加真实
浏览器的鼠标、原生文本、编辑键和窗口生命周期验收入口。

2026-10-21，继续[默认刷新率调查](firefox-default-refresh.md)。原版 Alpine ARM64
Firefox 142.0-r0，Release App FFI，4 CPU，总提交和单地址空间各 4 GiB，离线 HTML。
保留默认内容沙箱、Wayland proxy 和刷新率偏好，没有修改 Firefox/libxul ELF。
Linux x86_64 开发宿主的结果不代表 iPad、联网或日常浏览验收。

**默认 Firefox 偏好的 headless 组首次完成完整离线页面截图：正常退出 0，导出
640×480 PNG，精确检查到 60,000 个绿色 canvas 像素。** 这证明 HTML、页面
JavaScript 和软件截图渲染可以完成；Wayland/Xwayland 交互窗口仍未通过目标页面，
不能据此宣称桌面浏览器已可用。

## 运行时改动

私有 `FUTEX_WAKE`/`FUTEX_WAKE_BITSET` 原先即使没有匹配等待者，也会停止同伴并
回到协调器。现在在 joined 边界按 VM/地址汇总不可变的 waiter bitset，worker
仅在数量为零或没有匹配 waiter 时完成快速返回。保留已有地址、对齐、数量和
bitset 检查；真实唤醒、共享键、WAIT 和 REQUEUE 仍经过原协调路径。
seccomp 只有 ALLOW 可以进入快速返回，其他动作保留原来的执行和信号路径。

一个 epoch 内没有服务可以安装或搬移 futex waiter。遇到真实 WAIT 的 worker
仍触发会合，协调器在比较当前字节后才入队，因此并发的空唤醒不能跳过 WAIT
的比较/入队边界。没有增加 guest 字节锁、屏障或算法级线程安全保证。

串行路径也保留有界的纯查询执行：时钟、进程/用户身份和 getcpu 返回后继续
当前 quantum，避免每次查询后扫描所有休眠任务。最多连续 32 次立即返回，
仍受原指令量和 2ms 限制；待交付信号、实际信号跳转、阻塞和其他调用回调度器。
这补上了已有 worker 查询优化之外的单个 runnable 线程场景。

临时诊断观察到某个 10 秒区间约 3.3 万次 parallel round，但这个数字不是全部
额外开销：后半程的运行时账户里，Renderer 和 SwComposite 占了大量执行时间。
宿主叶 PC 采样同时有解释器 SIMD/浮点处理、快照更新和线程同步，不能把页面
停滞都归为协调器，也不能用 Linux 参考的浮点/SIMD比例预测原生 ARM64 表现。
临时统计与栈采集没有进入正式代码；带采样/日志的组不作为 App 启动时间基准。

## GPU 探测与输入依赖

Firefox 的默认 APK 依赖不包含所有运行时加载的 Zink 组件。第一次 GPU 复查缺少
`mesa-dri-gallium`、`mesa-gles` 和 `vulkan-loader`，不能作为 GPU 兼容性失败依据。
准备工具新增 `--gpu`，收集并安装这些原版签名包；探针在浏览器启动前检查必需
库，记录图像能力的类型，避免把 SHM 窗口误记为 GPU 呈现。

Linux 参考 Vulkan 是真实的 CPU physical device，原版 Zink 默认会排除它。
探针与已有 Zink 验收一样，只在显式 `TRAMES_ZINK_CPU_REFERENCE=1` 时给 guest
设置 `LIBGL_ALWAYS_SOFTWARE=true`，并记录为诊断。它用于选择 CPU Vulkan 参考，
没有换成另一套 OpenGL 实现；Apple/MoltenVK GPU 验收不得使用这个参考选项。

补齐依赖和参考驱动选项后，原版 `/usr/lib/firefox/glxtest --wayland` 通过：
实际创建 EGL/GLES context，返回 Zink renderer、Mesa vendor 和
`OpenGL ES 2.0 Mesa 25.1.9`。检查不仅看退出码：原版工具遇到一些图形错误仍会
退出 0，因此新增 graphics gate 要求 renderer 字段，并拒绝独立的 ERROR 记录。
这项工具没有绘制浏览器窗口或页面，不能当作 WebRender GPU 验收。

Firefox 142 的硬件 WebRender 在 GLES 路径会设置 `PREFER_ES3` 并请求 GLES 3
context，见原版
[GLContextProviderEGL](https://github.com/mozilla-firefox/firefox/blob/FIREFOX_142_0_RELEASE/gfx/gl/GLContextProviderEGL.cpp)。
当前独立探测只得到 ES2；后续需按真实的 Mesa/bridge 能力扩展并验证更高 profile，
不能通过伪造版本、扩展或 Firefox 探测成功来启用硬件 WebRender。

## 当前页面结果

恢复时的默认 Wayland 组观察 240 秒、带临时采样的组观察 300 秒，都没有目标 JS
标题和绿色 canvas。最终生产 FFI 组补齐 GPU 输入后再次保留默认偏好观察 240 秒：
约 94 秒出现窗口，但 JS/canvas 仍未通过，实际获取的是 SHM 图像；结束诊断完整。
这些配置、日志与缓存状态不同，不计算修复前后的端到端加速比。

同版 FFI 的 X11 组通过原版 Xwayland 进入原生 XWM，约 92 秒出现 Firefox 窗口，
输出图像是 native GPU WSI 类型；这表示 Xwayland 的 glamor 呈现，没有证明
Firefox 自身的 WebRender 已 GPU 化。目标文件已进入父侧 OpenDocument，
但观察 240 秒仍无目标 JS 标题，结束诊断完整。该组启用 DocumentChannel 日志，
没有做最终 canvas 像素回读，不把窗口图像数当作 FPS 或网页通过。

最后的 headless 组不设置任何 Firefox 偏好，不启用 MOZ_LOG/周期导出，保留默认
内容沙箱配置、Wayland proxy、4 CPU 和 4 GiB 限制。约 95.52 秒正常退出 0，
导出的 640×480 PNG 包含恰好 300×200=60,000 个目标绿色像素，结束诊断完整。
页面的绿色只由 JavaScript canvas 绘制，因此同时验证脚本执行；不是仅检查存在
一个 PNG 文件。headless 不创建窗口，frame 计数为零符合这个模式。
Linux CPU Vulkan 参考选项与上文相同；不将这个结果算作 GPU、联网或 iPad 验收。

这支持下一步重点拆分交互窗口的刷新、软件合成和主线程任务调度，但不同模式的
初始化也有差异，尚不能据此认定所有剩余等待只来自 Vsync。尤其不能用 glxtest
通过替代 GLES3/WebRender 或完整窗口页面验收。

## 验证与复查

Core 98 项通过，1 个手工基准保持忽略；严格 Core/FFI Clippy、Rust 格式、Python
语法和差异检查通过。新增回归覆盖空唤醒、真实 waiter 回退、bitset、VM 分离、
不可变旧快照、参数错误、seccomp 动作和连续 worker 指令量。串行回归用 500 次
身份查询检查有界 control 次数，并检查精确指令预算。

原版 ARM64 SMP fixture 在 4 CPU 下通过 futex 比较/入队/唤醒竞态、原子发布、
并发 fork、exec/exit_group；执行文件按原有 harness 导入 `/smp` 供子进程 exec。
源码未变的 CPU、Memory、VFS、网络、Vulkan、Wayland/UI 验收继续复用。

准备全新输入并检查 Firefox 自身的图形探测：

```sh
python3 tests/integration/firefox-prepare.py --output target/firefox-new --gpu --xwayland
python3 tests/integration/firefox.py \
  --store target/firefox-new/store --output target/firefox-graphics-new \
  --stage graphics --gpu --timeout 60
```

Linux 参考还需要原有 Vulkan loader/driver 环境和显式 CPU-reference 选项。
`--env NAME=VALUE` 可启用 `MOZ_GFX_DEBUG`/`EGL_LOG_LEVEL` 等上游诊断，但不能
覆盖探针控制的沙箱、后端和日志选项。所有 override 都写入本地报告。

探针另支持 `--backend x11`：原版 Xwayland 通过 `-displayfd` 报告就绪后才启动
Firefox，经原生 XWM 进入现有窗口系统；不是固定 sleep 后猜服务器已就绪。
它用于区分窗口后端与浏览器内部加载问题，不把 Firefox X11 GLX/WebRender 或
canvas 验收视为已经完成。headless 也可增加 `--capture-shm` 检查导出 PNG 的
真实绿色像素，而非只检查截图文件名。

原始 Firefox 日志、采样、栈、截图和运行数据只保留本地；本轮仅发布源码和工程
结论，没有上传诊断包或构建 IPA。[Issue #33](https://github.com/kb5000/trames/issues/33)
继续跟踪。
