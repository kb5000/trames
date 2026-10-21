# Firefox 交互与全局刷新任务

后续：[GLES3 与代理 WSI 调查](firefox-gles3.md)已补齐连接能力、descriptor
批量与采样状态，继续定位原版浏览器的实际 GPU 绘制调用。

2026-10-21，接续[默认 headless 页面验证](firefox-resume.md)。使用相同的生产
Release FFI、原版签名 Alpine ARM64 Firefox 142.0-r0，4 CPU、总提交和单地址空间
各 4 GiB。保留内容沙箱和 Wayland proxy，离线页面；Linux CPU Vulkan 参考选项
仍显式记录为诊断。没有修改 Firefox/libxul、App 默认值或 guest 时钟。

## 区分两个刷新来源

此前成功的低刷新率组同时修改了两个偏好。本轮拆开，每次使用独立的新 profile：

| Firefox 偏好 | 目标页面 JS 与实际 canvas |
| --- | --- |
| 仅 `layout.frame_rate=1`，保留默认 Wayland Vsync | 通过，精确 60,000 个绿色像素 |
| 仅 `widget.wayland.vsync.enabled=false`，保留默认全局刷新率 | 观察 240 秒未通过，绿色像素为 0 |
| 无偏好修改 | 复用上轮未通过结果，不重复运行 |

第一组约 95 秒出现窗口、155 秒完成目标页面；第二组约 97 秒出现窗口，结束诊断
完整。第一组有逐模块诊断日志；这些时间用于判断任务是否推进，不作为 App/iPad
性能或两种设置的加速比。

上游 [`gfxPlatformGtk::CreateGlobalHardwareVsyncSource`](https://github.com/mozilla-firefox/firefox/blob/FIREFOX_142_0_RELEASE/gfx/thebes/gfxPlatformGtk.cpp)
在 Wayland 上使用全局 software Vsync；默认软件频率来自 Firefox 自身的 60 Hz。
窗口的 [`WaylandVsyncSource`](https://github.com/mozilla-firefox/firefox/blob/FIREFOX_142_0_RELEASE/widget/gtk/WaylandVsyncSource.cpp)
则使用 `wl_surface.frame`。因此把 compositor 的帧回调修正到宣告的 60 Hz，
或只修改 `wl_output` 的刷新率，不会同时降低全局软件定时器的负担。
上游 [`VsyncParent`](https://github.com/mozilla-firefox/firefox/blob/FIREFOX_142_0_RELEASE/dom/ipc/VsyncParent.cpp)
将通知投递到高优先级队列；结合此前实际队列和线程执行账户，继续支持刷新任务
与软件渲染挤占普通文档任务的方向，尚不能把所有延迟归为单个等待锁。

## 交互验收入口

`tests/integration/firefox.py --interact` 增加真实浏览器交互检查，必须配合
`--stage wayland --backend wayland --capture-shm`。它从已呈现像素定位两个文本框，
经生产 App compositor ABI 发送鼠标、evdev 按键和原生 text-input 请求，以页面
DOM 返回的焦点、内容和光标位置逐步确认。不会因命令成功入队或 frame revision
变化就判定成功。

覆盖两个文本框往返、中文和非 BMP emoji、左方向键、退格、方向键后继续原生
输入、End、窗口 resize 后的 DOM/scene/frame 尺寸及窗口关闭/正常进程退出。
每步最多等待 45 秒，失败记录具体阶段。输入内容由页面确认；不要求客户端必须
在每次编辑后回传完整 surrounding text，避免把可选的协议快照当作输入成功条件。
初次探针误加了该条件，页面实际已完成 Unicode 输入和退格，等待逻辑随后修正。

修正后的定向组约 152 秒完成页面，依次通过以上全部输入步骤。最终 DOM 精确为
第一框 `abZ🙂返回`、第二框 `第二框`；窗口 resize 到 800×600，DOM 视区和呈现
frame 同步变化，原 60,000 个绿色像素也通过。首轮焦点/文本投递仍有明显等待，
随后操作较快；不同路径包含 IPC、绘制和 title 回报，不能把各步骤时间直接当作
iPad 的按键延迟。

**整项交互 gate 仍未通过关闭/退出阶段**：关闭后 scene 已没有 Firefox 窗口，
但 45 秒内没有正常进程退出标记，结束诊断完整。进程仍有执行活动，尚不能称为
确定死锁。探针分别记录输入、resize 和窗口消失三个里程碑，只有关闭并退出 0
才设置最终通过；不会把窗口消失等同于全部进程清理完成。下一步继续分析默认
全局 Vsync 的负担与关闭清理，不重复运行已经确认的独立输入步骤作为全套验收。

低刷新率的定向诊断示例（Linux 参考驱动环境同前文）：

```sh
python3 tests/integration/firefox.py \
  --store target/firefox/store --output target/firefox-interaction-new \
  --stage wayland --gpu --capture-shm --interact --timeout 480 \
  --pref layout.frame_rate=1
```

该偏好只写入本次探针创建的 profile，并在报告中标为诊断；不是产品修复或默认
刷新率验收。探针不模拟 UIKit 的输入法界面、iPad 剪贴板授权弹窗或真实触摸。

## 原生软键盘退格

空 surrounding-text 快照还暴露了 App 的删除边界：原先 UIKit 的 `hasText` 和
`deleteBackward` 可能把未知的 guest 编辑器当作空文档，直接丢弃软键盘退格。
现在只要存在已启用的原生文本 context，Delete 就保持可用；当本地没有光标之前
的文本时，转发普通 evdev Backspace，由客户端在实际光标处处理。真实空文档或
开头处的退格仍由客户端自然处理。已知文本的 UTF-8 字节删除、选择替换和输入法
marked-text 删除保持原逻辑。

增加 v1/v3 的 UIKit 回归用例，覆盖空快照转发和随后本地已知 emoji 的四字节删除。
Linux 宿主没有 Apple SDK，不能执行该 XCTest；生产 ABI 的真实 Firefox 按键验证
与 UIKit 用例分别报告，后续 Apple CI/真机仍需检查软键盘路径。

## 后续图形边界

当前 glxtest 只验证到 GLES 2；硬件 WebRender 会请求 GLES 3，不能据 ES2 探测
通过就声称 Firefox 已经使用 GPU 渲染。可继续分别验证原版
`gfx.webrender.software.opengl` 的软件绘制加 OpenGL 合成路线，以及真正的 GLES 3
WebRender 路线，按真实扩展和像素输出验收，不能伪造驱动版本或能力。

Firefox 142 的原生 Wayland compositor 还有独立前置条件：上游 GTK 初始化检查
HDR、DMABuf/格式能力、`wp_viewporter` 等；当前 Trames 没有暴露 viewporter。
它不能作为已经可用的通用捷径，也不能仅宣告一个空协议来绕过 Firefox 检查。

本轮原始日志、截图和运行转储只保留本地，不提交公开分支。源码未变的 runtime、
CPU/Memory、Vulkan 和 Wayland 通过结果继续复用；本轮不构建 IPA。
