# Firefox 文档任务：已入队，高优先级 Vsync 积压

2026-10-20。继续 [文档通道调查](firefox-docchannel.md)，这次追到父侧
`MessageTask` 的实际投递、待执行树和 `Run`。原版 Alpine ARM64 Firefox 142，
4 CPU、总/单进程各 4 GiB，默认沙箱和 Wayland proxy，网络/GPU 关闭。
正式运行时代码仍为 `95830f6`，没有修改 Firefox ELF 或正式 CPU/内存/系统调用实现。

**目标文档不是投递失败：它已经在 Firefox 的 TaskController 待执行树里。**
同时发现数百个高优先级 Vsync 通知积压。降低刷新率的诊断组让目标任务进入
`Run`，随后父侧完成 `DocumentChannelParent Init` 并进入 `OpenDocument`。
但 JS 标题和绿色 canvas 仍未通过；这既不是 Firefox 可用验收，也不是已经修复了
全部页面加载问题。

## 默认组的直接证据

临时诊断只把原版 ELF 的选定 PC 强制交给已有参考指令处理器，原指令仍正常执行，
并在每次到达时记录寄存器和有限的 guest 数据。它不再依赖冷解码首次到达，能够
看见反复执行的投递/执行路径。时间会受诊断影响，不用这些组计算启动加速比。

`task-controller` 组目标文件消息是 seqno=-55、`MessageTask=0x87c3f2b90`。
结合目标 URI 构造时间、通道和任务指针关联，不能只把 seqno 当跨通道唯一 ID。

- `ThreadEventQueue::PutEvent` 返回 1，说明投递成功。
- 包装它的 `RunnableTask=0xc857fdd30` 确实在 TaskController 的主线程待执行树中。
  110–165s 的每次 5s 采样都找到同一对象，不是消息被扔掉或仅仅漏了模块日志。
- 该任务 seqno=10893，优先级 4（Normal），依赖数 0，TaskManager 为空，
  `mInProgress=false`。没有它自身的管理器挂起或依赖等待证据。
- 同一待执行树中，`dom::VsyncParent::DispatchVsyncEvent` 的包装任务优先级为 7。
  对照原版 vtable、调用链和 `PVsync::Msg_Notify` 字符串确认身份，不为 stripped
  ELF 套最近符号名称。145s 父主线程栈正在这条 Vsync 发送路径。

| 运行时采样时刻 | 待执行总数 | 优先级 7 总数 | 其中 DispatchVsyncEvent | 优先级 4 总数 |
| --- | ---: | ---: | ---: | ---: |
| 100s | 253 | 157 | 155 | 20 |
| 105s | 532 | 407 | 405 | 37 |
| 110s | 286 | 129 | 129 | 80 |
| 120s | 710 | 458 | 457 | 140 |

[队列摘要](firefox-taskqueue/task-controller-queue-summary.json) 把遍历到的不同节点数
与原始 `std::set` 大小核对，避免只凭一个根节点猜积压规模。[目标任务记录](firefox-taskqueue/target-queue.log) 保留投递与
持续排队的原始行。

TaskController 优先执行较高优先级，再按任务创建序号排序。以上说明 Vsync 拥塞
是实际存在的调度障碍；仍需检查普通任务的推进、全局挂起计数和其他定时事件，
不能据此宣称所有等待都只有这一个原因。

## 低刷新率对照

探针增加显式 `--pref NAME=JSON`，只写入本次唯一的新 profile 的 `user.js`，并在
结果中记录。示例：

```sh
python3 tests/integration/firefox.py \
  --store target/firefox/store --output target/firefox/low-rate-new \
  --stage wayland --timeout 195 --capture-shm \
  --pref layout.frame_rate=1
```

保存的对照组还使用相同的临时诊断 FFI、MOZ_LOG 和每 15s 浏览器日志导出，完整
命令及校验和见结果/manifest。上述简例使用正式 FFI，便于复查，不能与带诊断组
直接当作计时 A/B。未提供 `--pref` 时不写 `user.js`，默认 Firefox 配置不变。

对照组中 `layout.frame_rate=1` 已从 guest 的 `user.js` 和 Firefox 写出的 `prefs.js`
再次导出确认。100–165s 共同采样区间内，DispatchVsyncEvent 的单次积压峰值从
默认组 457 降至 68；延长观察的低刷新率组后来仍有积压，不能称它已解决调度问题。

目标任务 seqno=-51，`MessageTask=0x889e91a10`，收到消息到分发仍等了约 58.91s，
随后进入 `Run` 和 `DispatchMessage`。Firefox 自身日志接着打印目标 file URI 的
`DocumentChannelParent Init` 与 `OpenDocument`，见
[执行记录](firefox-taskqueue/low-rate-target.log) 和
[父侧目标日志](firefox-taskqueue/low-rate-browser-target.log)。这推进了上一轮停在
父侧分发之前的检查点。仍不能只凭一次新 profile 对照声称因果已完全隔离，
下一步要区分 Vsync 生成/合并、普通任务饥饿，以及 OpenDocument 后的等待。

| 组 | 页面观察时限 | JS 标题 | 绿色 canvas 像素 | 最终 guest 诊断 |
| --- | ---: | --- | ---: | --- |
| task-controller，默认配置 | 155s | false | 0 | 完整 |
| low-rate，诊断偏好 | 195s | false | 0 | 未完整收取 |

两组都未观测到 Firefox 自行退出；探针失败来自页面验收。低刷新率组宿主浏览器
日志导出覆盖到约 180s，额外 20s 最终 guest 诊断仍未完整收取；不把缺失当无错误。
这里没有把低刷新率设为产品默认，也没有在运行时识别 Firefox 消息并改变其优先级。

## 可复查材料与验证

[manifest](firefox-taskqueue/manifest.json) 与
[压缩证据包](firefox-taskqueue/evidence.tar.gz) 包含五次独立新 profile 的结果/原始
日志、四版临时诊断 patch、原始 TaskController 元数据、选定父主线程栈及展开。
`sink-stack` 的固定对象地址与当次分配不一致，且一次跨越栈映射末端的整段读取常
失败；这些对象转储不用于结论。后续改为动态队列地址并逐页读取，在首个不可读页
停止。具体 PC/布局只适用于记录的 libxul SHA256/build ID 与父侧加载基址。

本轮正式改动是探针的诊断偏好选项和这些资料。Python 语法检查通过；五种非法
偏好值在创建 session/输出目录前拒绝，实际 Firefox 确认有效偏好已读取。
队列摘要节点计数核对、文件校验和、差异检查通过。正式 Core/CPU/FFI 已恢复并
核对原校验和，复用 [Linux / Apple ARM64 16KiB 定向验证](https://github.com/kb5000/trames/actions/runs/39414333180)，
不重跑源码未变的 CPU、内存、Linux、GPU/UI 项目。没有构建 IPA。

上游 Firefox 142：
[MessageTask 投递与执行](https://github.com/mozilla-firefox/firefox/blob/FIREFOX_142_0_RELEASE/ipc/glue/MessageChannel.cpp)、
[TaskController 优先级/分发](https://github.com/mozilla-firefox/firefox/blob/FIREFOX_142_0_RELEASE/xpcom/threads/TaskController.cpp)、
[VsyncParent 逐次投递](https://github.com/mozilla-firefox/firefox/blob/FIREFOX_142_0_RELEASE/dom/ipc/VsyncParent.cpp)、
[SoftwareVsyncSource](https://github.com/mozilla-firefox/firefox/blob/FIREFOX_142_0_RELEASE/gfx/thebes/SoftwareVsyncSource.cpp)。
[Issue #33](https://github.com/kb5000/trames/issues/33) 保持开放。
