# Firefox 文档加载：消息已抵达父侧，继续查主线程分发

2026-10-20。原版 Alpine ARM64 Firefox 142.0-r0，4 CPU，总/单进程均 4 GiB，
默认沙箱与 Wayland proxy，网络/GPU 关闭。正式运行时代码保持 `95830f6`；
本轮临时诊断修改的是解释器/协调器的日志，未修改 Firefox ELF。网页的 JS 标题与
绿色 canvas 仍未通过，不能算 Firefox 可用。

后续已确认目标任务成功进入 TaskController 待执行树，发现高优先级 Vsync 积压；
低刷新率对照推进到父侧 Init/OpenDocument，页面仍未通过，见
[任务队列调查](firefox-taskqueue.md)。

## 已确认到哪一步

上一轮只看到 DocumentChannelChild 构造。本轮对照原版 ELF 反汇编、首次到达某条
指令时的冷解码 PC/寄存器、Firefox 模块日志，以及 Unix socket 数据，推进到：

1. 目标 file URI 的 `AsyncOpen` 已进入，并完成 load group 的 `AddRequest` 回调。
2. 到达 `SendPDocumentChannelConstructor` 调用前后，返回的 actor 非空；随后
   `AsyncOpen` 返回 0。目标对象地址与浏览器构造日志一致。
3. 携带目标 URI 的消息在子侧 `sendmsg` 成功，父侧 `IPC I/O Parent` 的 `recvmsg`
   收到。1108 字节这条消息，类型 `0x72000f`、seqno=-49：发送时刻 110.016535s，
   接收 110.016908s，相差约 0.373ms。记录的长度、头部与 URI 一致；没有记录
   整包校验和，不把局部对照称为全包字节验证。
4. Firefox 随后打印父侧 `Receive from link; seqno=-49`。在覆盖到的后续日志中，
   未见对应主线程 `DispatchMessage` 或目标 URI 的 `DocumentChannelParent Init` /
   `OpenDocument`。seqno 不是跨通道的唯一标识；这里结合时间、方向与 URI 定位。

这将当前检查点收窄到父侧 `MessageTask::Post` 至执行/分发这段。不能再把该次
停滞解释为“目标 URI 没到父进程”。仍未确定是任务未成功入队、未被唤醒、优先级/
依赖阻塞，还是其他分发条件；本轮没有据此猜测修改 socket、MMU 或原子语义。

冷解码跟踪只记录缓存未命中时首次执行到的 PC，不是每次执行的完整指令轨迹。
目标文件通道的记录见 [pc-target-asyncopen.log](firefox-docchannel/pc-target-asyncopen.log)，
传输对照见 [wire-delivery.log](firefox-docchannel/wire-delivery.log) 与
[浏览器接收日志](firefox-docchannel/wire-browser-excerpt.log)。

## 排除与诊断限制

父主线程的一组栈停在 `nsThread::Shutdown` 的嵌套事件循环。读取上下文确认等待
名称为 `DOM Worker`。新配置组在 105/110/115s 仍处于这一调用，后来离开；
它不能直接解释剩余页面问题。复用配置组也保留了原版 ELF 的 `.eh_frame` 展开结果
及手工核对的 musl 栈序言；没有为 stripped libxul 猜最近符号名称。

此前 LR `0x4003a360` 的归属也得到更正：它是 Firefox 主 ELF 中
`mozilla::TimeStamp::Now(bool)` 调用 `clock_gettime` 后的位置，原先推测成 GLib
不准确。另有真正的 GLib poll 栈。Linux 5/6 时钟编号修复仍由独立 ABI 测试验证，
不能用错误的符号归属来证明该修复已解决页面加载。

本轮各组都带诊断，不能比较启动加速比例：

| 组 | 页面观察时限 | 结果与限制 |
| --- | --- | --- |
| stack | 210s | JS/canvas 未通过；最终诊断完整 |
| reused-profile | 155s | 与 stack 复用 profile，仅用于定位；最终诊断完整 |
| aborted-instrumentation | 100.60s 提前结束 | 临时指针跟随代码未检查加法溢出，触发本地诊断 panic；不是 Firefox/正式运行时崩溃 |
| shutdown | 145s | 独立新 profile；JS/canvas 未通过，最终诊断未完整收取 |
| pc | 160s | 独立新 profile；确认目标 AsyncOpen 成功；最终诊断完整 |
| wire | 165s | 独立新 profile；确认父侧收到目标消息；最终诊断未完整收取 |

wire 的最后一次宿主浏览器日志导出覆盖到启动后约 150s；最终 guest 日志收取
额外等待 25s 仍未完成。后续日志缺失不当作“没有错误”，也不能据此断言任务永远
不会执行。所有组都未观测到 Firefox 自行退出；失败判定来自页面验收未通过。

## 本轮实际改动与验证

修正 Firefox 探针的 profile 隔离：过去只用输出目录的 basename，`baseline/run`
与 `candidate/run` 会碰撞。现在每次加 monotonic nonce，并在结果中记录路径。
本轮明确把已发生的复用组标记出来，后续新配置组的命令和结果保留各自路径。

Python 语法检查通过。正式 Core/CPU 源码与 Release FFI 已恢复并核对校验和；
复用 [95830f6 的 Linux / Apple ARM64 16KiB 定向验证](https://github.com/kb5000/trames/actions/runs/39414333180)，
没有重复运行未改动模块，也没有构建 IPA。临时 patch 只作诊断复现材料，不在正式
源码中启用；aborted 组的错误诊断不应应用。

参考上游 Firefox 142：
[AsyncOpen](https://github.com/mozilla-firefox/firefox/blob/FIREFOX_142_0_RELEASE/netwerk/ipc/DocumentChannelChild.cpp#L67)、
[父通道 Init](https://github.com/mozilla-firefox/firefox/blob/FIREFOX_142_0_RELEASE/netwerk/ipc/DocumentChannelParent.cpp#L33)、
[MessageChannel 收到消息后投递 MessageTask](https://github.com/mozilla-firefox/firefox/blob/FIREFOX_142_0_RELEASE/ipc/glue/MessageChannel.cpp#L994)。

[manifest 与压缩原始日志](firefox-docchannel/manifest.json) 记录组别、二进制/源码校验和及限制。
[Issue #33](https://github.com/kb5000/trames/issues/33) 保持开放，下一步跟踪父侧任务调度与实际页面运行。
