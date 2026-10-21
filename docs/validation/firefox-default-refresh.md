# Firefox 默认刷新率与过滤后系统调用的四核开销

后续：[默认 headless 页面与原版 EGL 探测](firefox-resume.md)补齐串行查询与空
futex 唤醒的快速路径，默认偏好的 headless HTML/JS/canvas 截图已通过；交互窗口
仍待排查，并区分 GPU 输入依赖、glxtest 与 WebRender 验收。

2026-10-21，继续[文件变更域优化](firefox-backing-domains.md)之后的默认配置调查。
本轮修复两条通用路径，没有修改 Firefox ELF、默认偏好或内容沙箱。

四核 worker 原先只为没有安装 seccomp 的线程准备快速系统调用快照。安装过滤器
后，允许的时钟、身份查询和现成的并行 I/O 也会退回全局 checkpoint；I/O 服务层
还有同样的整段禁用判断。现在 worker 持有不可变的过滤器链，按真实调用号、
post-SVC PC 和全部六个参数求值。只有 ALLOW 继续本地查询或请求 in-epoch 服务；
ERRNO、TRAP、KILL、未知动作以及不支持的调用保留原有协调路径。

过滤器程序和链通过 Arc 共享，派发只克隆链句柄，不复制程序或逐个克隆过滤器。
安装、TSYNC、fork/exec 和跟踪仍在 joined 边界处理，旧快照不会被安装操作修改。
I/O 请求携带实际 PC，协调器在任何 VFS 效果前再次验证。没有过滤器时直接 ALLOW，
避免为普通调用构造 BPF 输入；filtered futex/epoll 等不支持的 worker 调用直接回
协调器，只在那里执行一次过滤器，不在 worker 先重复求值。纯 NativeCall 和 guest 字节访问协议保持原样；
没有加入普通访存屏障、额外字节锁或宿主内核 seccomp。

原版 Firefox 日志还确认：合成器宣告 60 Hz，实际帧回调却能接近 100 Hz，追随
测试端的轮询速度。更快的调用路径反而会放大这个刷新反馈。现在按宣告的刷新周期
约束回调，只合并已显示提交的回调，未显示的新提交继续等待真实 presentation。
延迟回调由原生事件循环的定时唤醒推进，不依赖 UI 再画一帧；长停顿不追补一串
回调，时间戳保持前端时基及 32 位回绕。GPU FIFO 和图像所有权释放不受该限速控制。

默认配置仍未通过：只接通 worker 过滤器快照的四核组观察 300 秒，加入回调节奏
修正的最终组观察 360 秒，均没有目标 JS 标题或绿色 canvas；两组结束诊断完整。
后者日志确认回调不再超过宣告刷新率，但普通文档任务仍迟迟未完成。窗口 revision
和已获取图像的数量不是新的像素帧数；不能将这些数字当成显示 FPS 或页面成功。
保留默认内容沙箱、Wayland proxy 和刷新率偏好，总/单进程各 4 GiB，网络/GPU
关闭。下一步继续区分 Firefox 的高优先级 Vsync、回调空提交与普通任务饥饿，
不能把修正一处节奏问题当作解决全部默认页面加载障碍。

最终候选又关闭所有 MOZ_LOG 和周期浏览器日志导出，保留默认偏好观察 360 秒：
目标 JS 标题和 canvas 仍未通过，结束诊断完整。因此逐帧日志确有潜在测量干扰，
但不能解释全部阻塞。此前带逐帧日志的组不作为默认 App 的启动性能测量。
后续需要将未就绪事件、futex/epoll 的全局 checkpoint 和 Firefox 普通任务推进
继续拆开，尚不能仅靠窗口刷新数量断言是哪一个占主导。

验证包括：旧实现在允许过滤器下的四核循环查询回归失败；修复后查询由 worker
完成，不要求同伴 checkpoint。拒绝的 read 不发 I/O 请求、不消费管道或修改缓冲区；
strict 允许的 read 可以使用服务；过滤器快照覆盖 PC、第六参数高位、后续安装隔离
和动作优先级。原版 ARM64 seccomp fixture 在 1 / 4 CPU 下均与独立宿主 Linux
oracle 一致，覆盖 ERRNO、TRAP 参数/信号、TSYNC、fork/exec 和 KILL_PROCESS。
Core 92 项通过、1 个手工 benchmark 忽略；Wayland 7 项通过，包括周期、未显示
提交、旧呈现、停顿和时间戳回绕。严格 Clippy 和差异检查通过。

固定工作量用例见 `tests/fixtures/filtered-clocks.c`，对应的 App ABI 对照脚本为
`tests/performance/filtered-clocks.py`。四线程继承真实过滤器，每轮检查被拒绝调用、
单调时钟和进程身份；宿主原生 C oracle 通过。它用于测量系统调用协调成本，
不能当作 Firefox、CoreMark 或 iPad 的整体加速比。

Linux x86_64 共享开发宿主上，最终 Release FFI 的两次交错对照，每线程固定
2,000 轮、四线程：基线中位数约 0.350 秒，候选约 0.033 秒，约 10.5 倍。
包含 shell fork/退出与终端轮询，不含 rootfs 安装和文件导入。基线是此前保存的
文件变更域优化版 FFI（尚未提前截断失败通知）；这个用例不触发该截断错误边界。
本轮基线/候选运行之间没有其他活跃 guest；短小过滤器和轻量查询会突出同步开销，
两次样本不用于统计稳定性或真实应用预测。最终结果只按工程结论汇总，原始结果
和测试存储不提交到公开分支。

原始 Firefox 日志、运行转储及诊断包不纳入公开提交。Linux 开发宿主的结果不能
替代 Apple ARM64/16 KiB、实际 iPad、联网或 GPU 验收。本轮没有构建 IPA；
源码未变的 CPU、Memory、Linux dispatcher、GPU 和 UI 检查继续复用。
