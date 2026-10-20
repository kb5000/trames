# 降低后台磁盘读取的每次调用成本

基线 `d1e1c30`。大块读取继续异步执行，本批减少准备、宿主调用、分配和完成处理。
[架构与边界](../architecture/epoch-services.md)记录所有权与限制。

- 每个 I/O worker 缓存至多 16 个已验证的只读宿主句柄，使用 backing 的 Weak 身份，
  命中后用 pread；不再每次 open、两次文件状态查询和 seek。guest 准入、长度/offset
  仍每次取当前状态；按路径或整数 inode 复用句柄会混淆替换/重新打开的对象，故未采用。
- 每次读取前回收失效 Weak；250 ms 空闲后关闭全部缓存，再进入无定时器阻塞等待。
  Weak 不保留存储锁/路径，空闲回收也防止宿主 FD 长期保留已删除文件的物理块。
- 完成后的独占 Vec 移回协调器池，最多两份、各至多 1 MiB；再次提交仍移动所有权。
  等长读取保留初始化长度，短读 truncate，扩长初始化新增区域，不输出上次的尾部。
- 标量目的地址不用堆分配 iovec；pending 用可复用槽位，消费完成事件后才复用 ID。
  没有 pending 且无需大块准入时直接进入原服务，恢复单次 process 查找。

没有添加全局锁、共享热路径计数或 busy-spin，也没有把独立文件改为串行执行。
小读取、单任务、写入/sync、MappedData/共享 backing 继续原边界。新增缓存不触及 guest
内存并发模型，后台仍不持有 VFS Rc/RefCell 或 guest 页引用。

## 开销诊断

Linux x86_64/4 KiB 宿主，原版静态 musl、aarch64 guest、4 CPU、release。ptrace 在
当前环境不可用，使用仅诊断时 LD_PRELOAD 的 C 计数器，按 `trames-disk` 线程名统计
宿主 libc API；不把插桩运行用于时间比较，计数器不进入产品。

同一个 ELF 的 bulk 32：固定 256 次 64 KiB 数据读取与 2 GiB native 填充。由于调度，
其中 before 240 次、after 251 次在后台完成，其余走 joined 路径。对应宿主调用为：

| 后台调用 | before | after |
| --- | --- | --- |
| open | 240 | 2 |
| 文件状态查询 | 480 | 4 |
| seek | 240 | 0 |
| read / pread | 240 / 0 | 0 / 251 |
| close | 240 | 2 |

减少的是每次后台读取的重复准备，不是减少 guest 数据量。预热后的重复读取主要保留
一次 pread；第一次打开仍使用原 regular/O_NOFOLLOW/metadata 检查。

## 固定工作量对照

I/O-only 使用两个并发 pread 调用者，各 4096 次 64 KiB，共 512 MiB；无 native 填充
线程、无按时间循环。相同 ELF/store 初始化，输出对照本机 C，before/after 与 after/before
各一对。测量期间没有构建/其他测试。

| I/O-only | before | after | 耗时减少 |
| --- | --- | --- | --- |
| 第 1 对 | 0.232460 s | 0.217869 s | 6.3% |
| 第 2 对 | 0.215270 s | 0.171147 s | 20.5% |

累计 RPC 等待分别 0.414→0.383 s、0.383→0.295 s；多个调用者等待可重叠，不能视为
wall time 或 CPU 占用。总 syscall 都是 8226，固定数据读取为 8192；后台完成数分别
8068/8152→8181/8163，没有通过减少后台工作制造提升。guest 指令数最多相差 778
（<0.04%），调度相关执行路径不完全一致，未逐指令归因。两对样本有波动，只说明
此宿主和工作量中降低了链路成本，不推断 App/iPad 或普遍性能提升。

原混合负载每次还有固定 16 GiB native 填充，native 调用/字节不变：

| 模式 | before 两次 | 最终 after 两次 |
| --- | --- | --- |
| 单读取者＋native | 0.229761 / 0.220112 s | 0.219668 / 0.210175 s |
| 双读取者＋native | 0.237564 / 0.277289 s | 0.220274 / 0.233023 s |

最终候选补充了失效/空闲句柄回收，复用此前精确基线，只重跑改变的候选；不是重新
交错的负载控制。初始交错结果保留在 prototypes，不作为最终成绩。部分样本后台服务
数不同，混合负载总 syscall 最多差 2，不能把耗时变化全归于缓存；原始计数全部保留。

默认缓存镜像 Docker：run 到 shell **22.225→22.701 s**，daemon **10.200→10.099 s**，
exit/--rm **1.801→1.875 s**。同一 default-probe/1741-inode store、4 CPU/4 GiB，默认
managed containerd/runc/overlay2，无拉取或特殊配置。仍没有证明 Docker 提速，也未
复现 iPad 上的长时间启动。下一步仍是写入、manifest/metadata、网络/PTY/proc 及
checkpoint 等待；未做新 CoreMark 或 IPA。

## 正确性与复用

新增用例验证同一缓冲/请求槽连续完成 12 次并正确停止；缓存句柄观察 pwrite、截断/
扩长，短读不泄露旧尾；同路径替换后的旧/新 inode 内容独立；缓存不保留存储锁，
关闭后重新打开 store 不命中旧对象。原 stop/不重放、共享 offset、独立 pipe 用例保留。
新增原版 musl 三线程混合模式，两个读取者可同时提交。

初批 Core79/VFS163/Linux24、严格相关 Clippy、27 组原版 musl 对照与生产三终端通过。
最终清理变更只重跑相关 Core3/cache3、严格 Clippy 与两个磁盘模式×1/2/4 CPU 六组，
其余通过结果复用；合入 CI 检查最终代码的完整受影响模块与 Apple ARM64/16 KiB 下
全部27组、生产三终端。没有重跑未改的 CPU/MMU/GPU/UI/CoreMark。

源码、ELF/CLI/FFI 与原始证据 SHA-256 见 [manifest](disk-cost/manifest.json)。
