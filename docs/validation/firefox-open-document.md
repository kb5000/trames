# Firefox 文件读取、挂起计数与原子访存调查

后续已在单核诊断组完成目标 HTML / JS / canvas 验收；默认四核仍未通过，见
[重定向及四核跟踪](firefox-redirect.md)。下文保留本轮当时的失败结果。

2026-10-20，继续 [TaskController 调查](firefox-taskqueue.md)。原版 Alpine ARM64
Firefox 142、总/单进程各 4 GiB、默认沙箱和 Wayland proxy，网络/GPU 关闭。
这轮修复了 [Node 的 16 KiB 邻页快照问题](node-repl.md)，以及调查中确认的
[串行 LL/SC 与私有文件原子 COW 问题](firefox-exclusive.md)。
**Firefox 的 JS 标题、绿色 canvas 验收仍未通过，Issue #33 继续开放。**

## 文件确实可打开，挂起计数未见异常

`manager-file` 组延长到 300s，并在既有临时诊断中加入 TaskManager 元数据与
目标文件 openat 的真实返回值。`layout.frame_rate=1` 仍只是诊断偏好。
目标为 `file:///tmp/trames-firefox-130168740180573.html`；父侧进入 OpenDocument，
随后 174.220830s，父 VM 的 StreamTrans 线程 pid 164 成功打开它，返回 fd 68。
因此不能再把这一段解释为“目标文件没有打开”或直接把失败归给路径/磁盘读取。
本组没有记录目标 read 的返回值；open 成功不能代替完整数据传输证据。

两组 100–316s 共 88 次挂起计数快照都核对了完整待执行树：遍历节点数与原始
std::set 大小相符。管理器计数等于树中该管理器的任务数，或多一个正在执行的任务。
空闲管理器挂起，Vsync 管理器未挂起；没有观察到全局挂起计数把普通任务都隐藏的
异常。这个结论只覆盖采样点，不证明所有时刻都正确。

普通任务仍积压。例如 manager-file 在 120s 有 154 个 Normal、112 个 Idle，
316s 有 298 个 Normal、195 个 Vsync、112 个 Idle。主线程的选定展开栈包含
GTK 原生事件处理、Wayland 隐藏窗口回调和同步 PCompositorBridge::Msg_Pause 等
等待，不能仅凭队列积压认定唯一根因。

`stream-pump` 组改用上游实际日志模块名 **nsStreamPump**；此前尝试的
nsInputStreamPump 不是该模块的名字。额外把 layout.throttled_frame_rate 设为 1
也没有解决积压，不能作为产品修复。该组 300s 内甚至未见目标父侧 OpenDocument。

## 单核对照揭示一个真实的重试循环

探针新增显式 `--host-cpus`，在打开生产 session 前设置宿主亲和性，记录实际
CPU 集合；默认仍为 4。单核诊断的 parallel rounds 为 0。
原实现中，60 次父主线程样本有 47 次落在同一 LDAXR/STLXR 重试范围，直到
300s 仍没有窗口。原版指令中间的一次无关栈写入使全 VM reservation token 失效。
[指令级回归与修复](firefox-exclusive.md) 保留了确切指令和修复前失败结果。

修复后，单核组在 180s 内绘制了浏览器窗口，并推进目标文件的 nsStreamPump
到 OnStateStart。仍未见 JS 标题或绿色 canvas；不同 profile 和临时插桩也意味着
不能把两组当作严谨的启动速度 A/B。四核默认偏好及最后的 COW 修复复测结果见
[manifest](firefox-open-document/manifest.json)。没有把低刷新率或单核设为产品默认。

## 证据和范围

[证据包](firefox-open-document/evidence.tar.gz) 包含各组结果、完整已导出的浏览器
日志、终端诊断、队列摘要、选定原始栈/TaskController 元数据、临时插桩 patch、
展开脚本，以及经过明确筛选的宿主 trace。保留全部映射/消息钩子/管理器记录，
父主线程和文件读取线程样本，以及 100/150/316s 存在的完整任务树；manifest
记录筛选规则、原始完整宿主 trace 的 SHA256 和包内文件校验和。
未筛选的完整资料留在工作区，压缩包没有冒充全部原始采样。

临时 FFI 仍基于 95830f6，固定 libxul 布局；它只用于前三组诊断。
后面的生产 FFI 使用 cf71f2d（串行 reservation）及 f36d16f（私有文件 COW），
没有加入 Firefox 专用 CPU 钩子。两次旧 trace 的原始栈分别保存，避免临时文件
同名覆盖。最终状态与是否完整收到 guest 诊断分别记录，不把日志缺失当作无错误。

本轮只验证变化的 Memory/CPU/Accelerator/Core 与 mapped VFS 路径，并在 Apple
ARM64/16 KiB 上重现及复测原版 Node。未重跑无关的网络、Vulkan 和 UI 验收；
没有构建 IPA。下一段重点是目标文件 OnStateStart 之后的通道重定向、数据回调和
普通任务推进，不能把目前的浏览器窗口当作 Firefox 可用。

## 各组页面验收结果

| 组 | CPU / 偏好 | 图像 revision 数 | JS / canvas | 最终 guest 诊断 |
| --- | --- | ---: | --- | --- |
| manager-file | 4 / 诊断偏好 | 464 | 未通过 | 未完整收取 |
| stream-pump | 4 / 诊断偏好 | 427 | 未通过 | 完整 |
| one-cpu | 1 / 诊断偏好 | 0 | 未通过 | 完整 |
| one-cpu-fixed | 1 / 诊断偏好 | 3381 | 未通过 | 完整 |
| four-cpu-fixed | 4 / 默认 | 687 | 未通过 | 完整 |
| four-cpu-cow | 4 / 默认 | 135 | 未通过 | 未完整收取 |

浏览器全部关闭后重新打开 store 导出 libxul.so，其 SHA256 仍与原始输入一致
（26b079d4…d04550）；这次没有检测到该 ELF 的持久内容变化。
