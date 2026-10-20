# Firefox：在 4 GiB 下修复稀疏 BSS 与线程创建

2026-10-20，继续使用原版签名 Alpine ARM64 Firefox 142.0-r0、Release FFI、
完整 hybrid musl 和新 profile。总预算、单进程预算均保持 **4 GiB**。
没有修改 Firefox/Scudo，也没有关闭其内容沙箱配置。
**页面首帧和截图仍未通过**，[Issue #33](https://github.com/kb5000/trames/issues/33) 保持开放。

## 确认的原因

原版 `libscudo.so` 文件约 132KB，RW ELF 段的 `FileSiz=0x45d`，
`MemSiz=0x20007488`，包含约 **512 MiB 零初始化 BSS**。
旧实现为这段建立普通页对象，并按整段映射计费；准备并行视图还会读取、复制零页。
多个 Firefox 进程因此很快耗尽 4 GiB 的映射计费额度。

临时 syscall 诊断捕获到 Scudo 的 `mmap(..., 0x20007000, PROT_READ|PROT_WRITE, MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, ...)`。
之后 1MiB 分配被拒绝时，该 VM 的映射量约 821MB、其他 VM 约 3473MB，合计接近 4 GiB。
这和 Scudo 的 `internal map failure`、随后 `mozalloc_abort` 相符。
小范围、不允许移动的 mremap 探测也会合法返回 ENOMEM，不能一并当作预算耗尽。
[原始 ELF 头和精选 syscall 记录](firefox-memory/scudo-evidence.txt) 保留了区分依据；
临时日志插桩已移除。

另发现 `CLONE_VM` 会先经 `Process::fork()` 克隆完整页表，再丢掉副本改为共享。
这次临时克隆还会退休父进程 MMU 视图。Firefox 创建大量线程时反复支付此成本。

## 实现

- 私有匿名零页保留不可变零存储。读取和冻结视图不复制整页；新 worker 物理槽
  使用宿主的零填充行为，复用过的槽先清理旧内容。
- CPU、原生函数和并行 worker 在首次写入前使用同一份页额度。
  同页并发首次写入只计一次，commit 不重复计费。系统调用和原生输出预检
  在消费输入前保留额度，失败不会丢掉文件/队列输入。页额度随映射/保留视图的生命周期释放。
  后续普通硬件存储使用 TLB 的 resident 位，不逐次执行额度原子 RMW。
- discard、unmap、remap 和 fork 保留字节、权限及 COW 语义。
  文件/共享映射先检查额度，再让 VFS 创建存储；固定文件替换可以复用已计费槽位。
  首次触页超额终止故障 guest 线程组（SIGKILL）；普通 mmap/fork 拒绝仍返回 errno。
  没有实现 Linux 的 OOM victim 选择。
- 共享线程直接建立共享 VM，不经过临时 fork。额度已满但只共享现有页的
  `CLONE_VM` 仍可成功，不新计一份内存。
- 独占监视器的约 2KB/页元数据延迟到第一次 exclusive read 才分配。
  没有既存 monitor 时，普通写入无需表；发布和后续行访问使用已有 region guard。
- 文件末尾之外的 VFS 页和 SIGBUS snapshot 使用零占位。
  文件增长后仍能变为可读写共享页；截断、私有副本和脏页提交沿用原检查。

文件映射、已物化的 fork COW 页仍按每个地址空间保守计费，尚未去重所有共享物理页。
每进程 mapped-page 上限仍用于限制页表元数据，包括尚未写入的可读写映射。
这不是完整 Linux 按需分页，也不是精确的宿主 RSS/PSS。
并行影子页、COW 窗口和宿主分配器存储不等同于 guest 字节额度。
并发边界见 [区域访存说明](../architecture/region-memory.md#sparse-zeros-and-first-write-budgets)。
本轮没有新的 ThreadSanitizer 结果，原有 sanitizer 记录不能替代新代码的验证。

## 验证与运行进展

原版 musl 静态 ARM64 探针声明 512MiB BSS，检查未触碰页、首次写入、fork COW、
discard 和 `/proc`。4GiB/4GiB、四 CPU 下输出：

```text
zero-bss bytes=536870912 virtual_kib=525392 resident_kib=80 COW/discard=PASS
```

宿主 Linux 原生 C 对照也通过；虚拟/常驻内存的绝对数字不作跨平台性能比较。
针对性单测覆盖同页并发、跨 VM 竞争唯一额度、原生写入、commit 不重复计费、
零页 COW、槽复用、独占/ABA、文件 EOF 增长，以及满额度共享线程和触页超额处理。
本地 Memory 71、Core 81、Linux 27、mapped VFS 9 项通过，Clippy 通过。
CI 只复核受影响模块和这个 ARM64 探针，包括 Apple ARM64/16KiB 宿主页；
复用未改动的 CPU opcode、GPU/UI 和 guest 构建验收，不构建 IPA。

| 诊断组 | 观察窗口 | 结果 | 宿主进程 HWM |
| --- | --- | --- | --- |
| 旧构建，无插桩 headless | 100s | 父进程仍在，无截图；完整收集日志 | 8.78GiB |
| 旧构建，临时 syscall 插桩 | 约 55s | Scudo OOM，主动 abort，exit 139 | 9.00GiB |
| 零页/线程修复后 headless | 100s | 无截图；末尾日志收集未在额外 15s 内完成 | 6.99GiB |
| 修复后 Wayland，SIGPROF 采样 | 180s | 无窗口/首帧，父子进程仍在；完整日志未出现 Scudo OOM | 5.11GiB |
| 关闭周期 guest 诊断，headless | 180s | 无截图，父子进程仍在；结束日志完整，未出现 Scudo OOM | 7.03GiB |

最终源码对应的低干扰 Wayland 组观察 120s，结束日志收集完整，仍未出窗口/首帧；宿主 HWM 为 5.11GiB。完整日志未出现 Scudo OOM。

这是单次分阶段诊断；guest 运行进展和观测开销不同，不换算整体加速比或 iPad 所需 RAM。
宿主 RSS 仍明显高于 guest 实际页额度，需要继续拆分影子副本、COW 窗口保留及 allocator 存储。
headless 的不完整日志也不能证明所有错误均未发生。最终候选构建另见
[manifest](firefox-memory/manifest.json) 和 `memory-candidate-headless` 记录。

采样使用与其哈希配对的 ELF 按符号范围解析；原始 PC 样本校验和保存在报告中。
40 秒后的 CPU 样本约 5.27% 落在 `Kernel::sync_resources`、3.86% 在
`Vfs::share_context_from`、4.76% 在 `Node::path_in`，另有约 39.97% 在宿主 libc。
这些是叶 PC 的 CPU 样本比例，不能当调用栈或墙钟占比，也不能把 libc 全归因于分配器。
这些历史诊断组每 15 秒执行 guest `/proc`/ps，探针本身也参与采样；
大量线程时命令可能排队，不能将其开销全归给浏览器。探针已改为默认仅在结束时
收集 guest 日志，`--guest-progress` 才开启周期诊断。下一步应按 syscall/线程拆分重复资源同步、
路径查找、阻塞任务重试及宿主 libc 的具体工作，再评估调度和第一帧阻塞。

`getrusage`、Netlink monitor bind 仍有缺口；它们尚未被证明是第一帧缺失的直接原因。
仍需实际验收软件渲染及 EGL/GLES/WebRender、默认沙箱约束和 iPad 内存行为。

## 复现

使用 [既有原版 APK 准备流程](firefox.md#复现)，保留隔离 store，顺序运行：

```sh
bash scripts/build-firefox-fixture.sh
target/release/trames-run --memory --memory-budget 4GiB \
  --process-memory-budget 4GiB --cpus 4 target/firefox-fixture/memory-probe
python3 tests/integration/firefox.py --store target/firefox/store \
  --output target/firefox-memory-wayland --stage wayland --timeout 180
```

不要同时打开同一个 store。探针会记录 guest `/proc` 和宿主 RSS；status JSON 中的
`budget_bytes` 是磁盘额度，RAM 配置在 `options.memory_budget_bytes` 中。
采样是可选诊断，不作为未插桩计时。证据和生产源码哈希见
[firefox-memory/](firefox-memory/manifest.json)。
