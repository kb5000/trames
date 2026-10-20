# Firefox：首轮运行时修复

2026-10-20，在 [初次评估](firefox.md) 后继续测试原版 Alpine ARM64 Firefox 142.0。
本轮修复系统接口和地址分配行为，没有修改浏览器、Scudo 算法或默认沙箱配置。
后续已继续做 [4GiB 内存修复](firefox-memory.md)。本文保留首轮的历史结果；
仍未把页面首帧/截图计为通过；后续跟踪在 [Issue #33](https://github.com/kb5000/trames/issues/33)。

## 已修复

1. **FIONREAD**：Unix stream、datagram、seqpacket、pipe 和原生 Wayland 桥接
   使用现有队列查询可读字节，不消费消息或 SCM_RIGHTS。datagram 返回首条长度，
   stream/seqpacket 返回队列总字节，空队列/EOF 为零，listener 返回 EINVAL。
   inotify 与已有 PTY 行为保留；此轮没有扩大到所有 IP socket。
2. **发送标志**：Linux AF_UNIX 发送路径忽略接收端专用的 `MSG_CMSG_CLOEXEC`。
   Firefox Wayland 代理在发送方向也带上此标志，原先因此返回 EOPNOTSUPP。
   接收端的 CLOEXEC 语义独立验证，发送不会改变原 FD 或接收 FD 的标志。
   也接受接收方向无效果的 `MSG_NOSIGNAL`，后续 audio IPC 曾因它被拒绝。
3. **SHUT_RD**：已排队的数据和文件描述符仍可读，阻止对端继续发送。
   原来直接清空队列、立即返回 EOF，与 Linux 对照不符。
4. **mmap 地址选择**：接受有效的调用者 hint 时，保持普通分配的搜索游标。
   原来高地址 hint 会把随后所有无 hint 的映射一起推到高地址。
   Scudo 的普通区域因此分散到很宽的虚拟地址区间，释放扫描变得极慢。
   固定映射、冲突 hint 的回退和成功分配后的游标推进分别测试。
5. **Unix socket FD 交接**：允许在独立通道之间通过 SCM_RIGHTS 交接 Unix FD，
   以及 Firefox broker 使用的“发送端把自身 FD 发送给对端”。
   按接收端队列的实际所有权图检查，仍拒绝会形成引用环的交接（EOPNOTSUPP），
   尚无 Linux 的通用 Unix socket 垃圾收集器。关闭端点在退出队列借用后释放消息；
   FD 链迭代回收，避免递归借用冲突和长链耗尽宿主栈。

队列查询不增加新的权限层或锁。FD 图检查只用于附带 Unix FD 的发送，
没有改 CPU 解释器、MMU、原生算法或日常访存路径。

## 如何定位 Scudo 停顿

临时诊断构建每秒记录 guest 线程的 PC、状态、寄存器和少量 ELF 标识，
诊断插桩已从生产源码移除。`Cookie` 线程持续执行位于 `0x10000000` 的
原版 `libscudo.so`，主线程在等待分配器锁。对应 PC 落在释放页的区域扫描和
互斥锁辅助函数；循环索引达到上亿量级，并非 `glxtest` 仍在执行。

LLVM 20.1.8 的 [primary32.h](https://github.com/llvm/llvm-project/blob/llvmorg-20.1.8/compiler-rt/lib/scudo/standalone/primary32.h)
通过最小/最大区域索引确定释放扫描范围，每个候选区域查询 byte map。
对照 guest mmap 策略，发现成功接受高地址 hint 后错误移动了全局普通游标。
修复后同机新 profile 的 Firefox 在约 30 秒内进入 Renderer、WebRender worker 和
子进程启动阶段；此前仅修 socket 的 120 秒组未达到该阶段。
这是定位进展，不是严格的性能跑分，不能据此计算加速倍数。

## Firefox 启动进展与当前边界

| 阶段 | 实测 |
| --- | --- |
| 只补 FIONREAD | 越过 ENOTTY，暴露 sendmsg 标志不兼容 |
| 再补发送标志 | 默认 Wayland 代理完成 GTK registry/seat/sync 握手；120 秒仍未出窗口 |
| 再修 mmap hint | 进入 Renderer，暴露 forkserver 的 Unix FD 交接限制 |
| 允许独立 Unix FD 交接 | Socket Process、forkserver、WebExtensions/Web Content 子进程可以启动；broker 自发送仍有拒绝 |
| 再允许安全的发送端自发送 | 无界面 4 GiB 总/4 GiB 单进程组约 59 秒出现 Scudo 分配失败，Firefox 随后主动崩溃，返回 139 |
| 最终生产构建、默认 Wayland 4/4 GiB | 90 秒未出窗口；子进程出现 Scudo OOM，父进程仍在。已无 sendmsg/recvmsg 不支持记录 |

Firefox 142 的 [SandboxBroker.cpp](https://hg.mozilla.org/releases/mozilla-release/file/FIREFOX_142_0_RELEASE/security/sandbox/linux/broker/SandboxBroker.cpp#l1003)
解释了自发送用途：响应后再附带一次发送端自身的 FD，规避内核 EOF 竞态。
这不是把接收端自身送进它的队列，不能按同一“channel 有环”规则一并拒绝。

末组明确记录 `Scudo ERROR: internal map failure (error desc=Out of memory)`。
`PC 0x40042138` 的空地址 fault 位于 Firefox 的主动 abort 路径；不能仅凭
SIGSEGV 就归因于 CPU 指令未实现。还需要区分配置预算、共享/COW 页计费和
尚未触碰的可读写映射。此轮没有降低记账要求、取消上限或修改 App 的默认内存设置。

另做总预算 8 GiB、单进程仍为 4 GiB 的诊断组：90 秒观察内没有重现上述
Scudo OOM，但也没有产生截图。这只说明预算会影响失败进程，不能据此证明
8 GiB 足够、内存记账已经正确，或浏览器可以使用。

图形探测还报告 `libEGL initialize failed` 和 `No GPUs detected via PCI`，
随后可见软件 WebRender 线程。尚未验证 Firefox 的 EGL/GLES/WebRender GPU 支持。`--gpu` 表示启用 guest 桥接选项；
本轮没有宿主 GPU 设备的原生对照，不将 EGL 初始化失败单独归因于 guest API 缺失。
`getrusage`、Netlink Monitor 的 bind 仍有不支持记录；audio recvmsg 的 `MSG_NOSIGNAL`
缺口已由 Linux 对照与 C 探针验证修复，尚未作为音频功能验收；
这些都需要继续处理，但不能把每项警告当成首帧缺失的已证实原因。

## 验证与复现

本地 Linux 使用 Release 生产 FFI、4 个 CPU、原 hybrid musl、原版签名 APK 与本地
JS/canvas 页面。没有使用代理绕过、禁用内容沙箱或关闭 Scudo 释放的环境变量。
默认配置保留不等于已经完整验证沙箱的约束效果。

有意义的检查包括：无效输出指针不消费队列、dup/EOF、数据报边界、零长包、
附带 FD 的生命周期、直接/间接引用环拒绝、安全自发送、接收截断后的释放，
以及 600 层 FD 链在 128 KiB 宿主线程栈上的回收。
新增 C 探针用原版 musl 静态构建，与宿主 Linux 对照；无需安装 Firefox 即可重现
socket ABI。Linux/Apple CI 只检查受影响 Core/VFS/Linux 模块和该 ARM64 探针，
复用未改动的 CPU/MMU/GPU/UI 验收，不构建 IPA。

```sh
bash scripts/build-firefox-fixture.sh
target/release/trames-run --memory "$PWD/target/firefox-fixture/socket-probe"
python3 tests/integration/firefox.py \
  --store target/firefox-fresh/store --output target/firefox-runtime-wayland \
  --stage wayland --gpu --wayland-debug --timeout 180
```

安装输入仍按 [原探测说明](firefox.md#复现) 准备。
工具增加 `--memory-gib`、`--process-memory-gib`，默认均为 4；覆盖预算的结果
必须按诊断记录，不能当成默认配置通过。各阶段的二进制哈希、命令和诊断日志见
[证据目录](firefox-runtime/)，生产源码哈希见 [manifest](firefox-runtime/manifest.json)。
