# Firefox：coarse 时钟与调度器大对象搬移

2026-10-20，沿用原版 Alpine ARM64 Firefox 142.0-r0、Release FFI、4 CPU、
总/单进程均 4 GiB、默认沙箱配置与 Wayland proxy。GPU 与网络关闭，目标仍是
本地 HTML 的 JS 标题及绿色 canvas。当前继续验收页面；不能把工具栏算作网页通过。

本轮确认并修复两个独立问题：

- Linux 编号 6 是 `CLOCK_MONOTONIC_COARSE`，原实现错误地返回 Unix 时间；
  编号 5 的 `CLOCK_REALTIME_COARSE` 则未支持。现将 5 与 realtime 对齐，
  6 与 monotonic 对齐，串行协调器和并行 worker 共用同一 helper。
  coarse 名称使用同一高精度虚拟时钟，`clock_getres` 仍报告 1ns；不是宿主硬件
  coarse tick 的模拟。CPU clocks 2/3 本轮未扩展。
- 调度器每轮会从 BTreeMap 取出、处理并插回任务。原来 map 直接存放 10768 字节的
  Process，节点移位及取出/插入会搬动 CPU/Linux/VFS 元数据。现在每个进程拥有
  一个 Box，map 只搬指针；不是复制 guest RAM，也没有改变并发字节访问和内存顺序。

## 定位证据

上一版不是完全没有内容进程：完整日志已有 `file:// Content` 和 Isolated Web
Content。延长到 900 秒后，仍只显示 New Tab/工具栏，JS 标题、绿色 canvas 均未通过；
Firefox 没有退出，最终诊断完整。收到 45065 个含图像的不同 window/revision，
这一计数不等于页面成功，也不当作屏幕呈现帧率。

SIGPROF 叶 PC 采样中，300–600 秒约 73.23% 的样本在协调线程，约 56.08% 位于
宿主 libc。热点地址的反汇编是大块搬移循环；另有 generic syscall 的返回位置，
不能把全部 libc 样本都归为复制。没有调用栈，不把叶样本比例当墙钟占比。
采样容量 200000，另丢弃 48446 条；最后保留时间见 manifest，600–900 秒分组只覆盖
容量耗尽前的部分。配对 FFI/符号表/原始样本校验和保存在 profile JSON。

另一次 10 秒宿主线程 CPU 计数：协调器约 8.08 CPU 秒、四个 CPU worker 合计约
3.78 秒、磁盘线程近零。只是该启动阶段的局部观察。

128 个真实 Process、1024 轮相同 remove/insert 的手工 Release 元数据基准：
按值存放 0.205899s，Box 指针 0.003475s。只测表搬移，不测 guest 指令或端到端应用，
不据此宣称 Firefox/CoreMark/Docker 有相同比例的提速。

在额外模块日志/线程状态诊断中，Firefox 与 WebExtensions 多次停在 musl
`clock_gettime` 返回位置、GLib 调用点，促成了时钟编号检查。该诊断在 241.05s
经宿主 SIGINT 主动停止，以测试已确认的修复；不是 Firefox 崩溃或默认验收通过。
时钟缺陷的 ABI 语义已经确定，仍需区分它与剩余页面问题的因果关系。

上游解释也核对过：

- [RenderThread::RunEvent](https://github.com/mozilla-firefox/firefox/blob/FIREFOX_142_0_RELEASE/gfx/webrender_bindings/RenderThread.cpp#L733)
  的 `Calling WaitFlushedEvent::Run: is delayed` 在事件到达、即将调用 Run 时打印；
  [WaitFlushedEvent](https://github.com/mozilla-firefox/firefox/blob/FIREFOX_142_0_RELEASE/gfx/webrender_bindings/WebRenderAPI.cpp#L869)
  随后完成同步任务。该条日志不是“永远没被唤醒”的证明。
- [GetPageFaultCount](https://github.com/mozilla-firefox/firefox/blob/FIREFOX_142_0_RELEASE/js/src/gc/Memory.cpp#L959)
  在 getrusage 失败时返回 0；这次没有用假成功消掉 ENOSYS，也未证明它阻止页面启动。

## 验证范围

本地 Core 87 项、严格 Clippy、fmt 通过。新增时钟测试核对 0/5 与 Unix 时间、
1/4/6/7 与会话 monotonic 时间的关系，也经过并行 View/Snapshot。
原版 musl ARM64 双线程 C 程序在 1/4 CPU 下通过；4 CPU 组实际使用两个 worker，
其中 1600 个系统调用在 worker 内完成。相同 C 程序在宿主 Linux 通过。
手工表搬移基准默认忽略，不加入每次 CI 跑分。

Apple ARM64/16KiB 的定向 CI 只重查 Core 和新 clock ABI；复用源码未变的
Memory/VFS/Linux/opcode/GPU/UI 验证。没有构建 IPA。

测试探针新增默认关闭的 `--moz-log` 和 `--browser-log-interval`，后者通过宿主
授权文件导出读取日志，不额外启动 guest ps/cat，单份日志最多 2MiB。
启用后明确标记为诊断组，不能与无插桩启动时间作严格比较。

原始结果、任务状态与校验和见 [firefox-content](firefox-content/manifest.json)。
[Issue #33](https://github.com/kb5000/trames/issues/33) 继续跟踪网页、交互与 EGL/GPU 验收。
