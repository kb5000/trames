# 普通文件在 worker 直接执行

> 本文为公开的验证汇总。提及的原始日志、二进制和逐项 JSON 记录保留在开发环境，未随本次源码提交公开。

2026-10-22，接续[文件服务交接](service-handoff.md)，将普通文件的字节、OFD 偏移和
活动元数据镜像拆成可共享对象。符合准入的文件短调用由执行 guest 的当前 worker 完成。
这是本地未提交工作树；本轮没有推送、IPA 或 release。

## 实现范围

- 内存和磁盘普通文件共用 Arc<PlainFile>，保留原 FileData 存取算法和同一份字节，
  发布能力不复制文件内容。读写按文件对象同步，没有 VFS 全局锁。文件长度单用
  relaxed 原子供元数据和配额查询，普通 guest 访存模型与原生函数执行位置不变。
- dup/fork/原 VFS 与 worker 共用同一个 OFD 偏移。顺序存取将实际传输和偏移推进放在
  一个对象事务内；定位存取不改偏移。lseek、文件锁相对位置和 proc fdinfo 也读取它。
- lseek/fstat，以及 read/write/readv/writev/pread/pwrite/preadv/pwritev 的允许路径在
  worker 内完成。Linux 预检、向量解析、gather/scatter 复用原 helper；错误、短传输
  和零长度不另写第二套 ABI。worker_file_syscalls 单列完成的文件调用，也计入已有
  worker_direct_syscalls 和 worker_syscalls。
- 小于 16 KiB 的磁盘存取使用保留 host 句柄；内存读取仍用原 1 MiB transfer cap。
  每个宿主 guest worker 最多缓存 16 个句柄，按 backing Weak 身份匹配，cohort 结束
  释放；数量不随排队 guest 线程增长。原异步磁盘池仍最多两个线程，各缓存至多 16 个。
- 大块磁盘读取继续异步交接。对象 lease 阻止 worker 写入与在途读取冲突，顺序偏移的
  pending 状态防止重复使用旧位置。冲突操作在产生效果前交还原等待路径，未向 guest
  返回人为竞态错误，也不回放已完成传输。
- atime、setid 清除、mtime/ctime 由共享元数据镜像维护。原 VFS、stat 和 manifest
  都读取同一活动状态，所有执行者返回后提交到捕获的 Node。Rc/RefCell 没有跨线程。
- 同一 FD 表和策略在 cohort 内复用能力数组；无文件直接跳过。每个新 cohort 重新
  判断映射/挂载状态，不能仅凭 FD 版本缓存 mmap 准入。快照不调用 host stat；只有
  实际 fstat 才读取当前磁盘 blocks，避免调度准备阶段扫描所有磁盘文件的元数据。

增长、append、同步写标志、有限 RLIMIT_FSIZE 写入、有 inotify inode/父目录观察者的
字节 I/O、MappedData、SharedFile/GPU backing、overlay 下层与动态伪文件保留原路径。
fsync/fdatasync、manifest 提交、FD/权限/映射/生命周期变化仍由协调角色处理。
本轮没有完全删除协调角色，也没有给普通 guest 字节添加更强的内存屏障。

## 正确性和集成

最终 VFS Release **189 项通过、1 项既有忽略**。新增七项共享文件检查覆盖：

- dup/fork 与四个真实 host 读取线程消费 1024 条唯一记录，和原 VFS 共用偏移；同表
  能力数组复用，策略变化另建能力。
- 磁盘覆盖写、stat/mode/时间镜像、活动期间 owner 写入及 sync 后重新打开的持久结果。
- 增长、append、有限文件长度和同步标志在字节/偏移改变前回退。
- inode/父目录 inotify 保留原通知；readonly bind 不允许写入或更改 atime。
- mmap 不变更 FD 表版本时仍在下一 cohort 排除该文件。
- 在途异步顺序读取与 worker seek/read/write 互斥，定位读取可继续，完成后只推进
  一次共享偏移并恢复覆盖写。

同轮首阶段 Linux **37 项**、Core **125 项**通过（Core 2 项既有忽略）。新增 Linux
检查遍历上述文件 syscall，验证标量/向量/定位内容和位置、stat、非法向量完整预检、
offset/指针错误顺序以及增长回退后恰好执行一次。后续只调整文件能力缓存、原子长度、
宿主句柄缓存归属及 stat 快照准备：重查最终 VFS 和受影响的生产集成，复用首阶段
Linux/Core 结果，没有为了文档再跑完整工作区。

静态 ARM64 原版 musl ELF 与独立 host C 断言/输出 oracle 验证：

- 首阶段 epoch-services：file/vector、private/shared mapped read、bulk-parallel ×
  1/2/4 CPU 共 15 组通过。mapped 文件没有误用直接 Plain 路径。
- 六读取者 service-handoff：pread/preadv/read × pinned/spread × 2/4 CPU 共 12 组
  通过，内容、唯一记录与总偏移正确。pinned 模式所有 guest 只允许 CPU 0，原生
  32 次 1 MiB 填充仍完成；定位读取在途峰值 2/4，共享偏移 read 为 1。
- 缓存/长度优化后的 file/vector/bulk-parallel × 2/4 CPU 共 6 组通过。256 次填充
  的 file/vector 用例本地文件完成约 4500–5300 次，协调服务完成均为 **2 次 sync**；
  另有一个未完成服务请求退回原执行边界，不能把完成数当成总请求数。
- 最终二进制的下面 24 次对照同样校验完整输出、固定原生调用数/字节数，原生
  fault/reject/fallback 均为 0。单 CPU 使用原 joined 执行路径。

最终五个包 VFS/Linux/Core/CLI/FFI 的 tests Release 严格 Clippy `-D warnings`、
Release CLI/FFI 构建、fmt 与 diff 空白检查通过。阶段和日志见
`worker-files/results.json`（本地验证记录）。旧 fixture/host oracle 直接复用，没有重建
未改 ELF、重跑 CPU/GPU/网络差分，或运行 CoreMark、Docker、Firefox。没有新的 QEMU、
Apple ARM64/16 KiB 页或 iPad 真机验收，也没有读取或复制私人诊断包。

## 一次固定工作量对照

基线为上轮 service-handoff 最终 Release，SHA-256 `43a1fff244747777c6e96cd0b27b4fe8d9fd8177fb53bae7e6e2696fdbc0bebf`。
同一 ELF，每个样本 8192 次文件操作循环，同时另一 guest 线程执行 1024 次原生
1 MiB 填充。file/vector 各自保持字节、位置、输出和原生工作量不变。
每组交替 before/after，再反向交替，各测两次；下表是墙钟中位数，包含启动和收尾。

| backing / 配置 CPU | 调用 | 修改前 ms | 修改后 ms | 协调服务完成数：前 → 后 |
| --- | --- | ---: | ---: | --- |
| 内存 / 4 | file | 74.3 | 54.4 | 886、943 → 6、8 |
| 内存 / 4 | vector | 78.6 | 56.0 | 876、564 → 8、6 |
| 磁盘 / 4 | file | 179.8 | 144.6 | 472、471 → 4、4 |
| 磁盘 / 4 | vector | 194.3 | 149.1 | 1123、501 → 4、4 |
| 内存 / 1 | file | 63.9 | 64.6 | 0 → 0 |
| 内存 / 1 | vector | 66.8 | 68.2 | 0 → 0 |

四核配置的混合工作量观察到约 20%–29% 时间下降；本地文件完成数为每次约
1.1 万–2.3 万。单核样本观察到约 1%–2% 更慢。这些样本不足一秒且每组只有两次，
不能分离噪声或把结果当成应用加速比、四核计算扩展性或 iPad 预测。并行阶段时长
会改变进入 worker 的调用数；不能按服务计数反推固定 syscall 总量。长样本可能跨
更多 fairness/cohort 边界，服务完成数也不必与上面 256 次填充用例的 2 次相同。

复现其中一组（使用已构建的原有 ELF/oracle）：

```sh
python3 tests/performance/epoch-services.py \
  --baseline /path/to/prior/trames-run --candidate target/release/trames-run \
  --host-oracle /path/to/epoch-services-host \
  --modes file vector --iterations 1024 --samples 2 --cpus 4 \
  --file-storage disk --output target/worker-files-disk-results
```

## 后续边界

文件增长/append 需要将容量预留、长度变更和持久化记录拆为对象操作；映射文件要与
FilePage 脏页一致性共用存取模型。大块读取的完成 scatter、socket/PTY、GPU 和共享
key futex 仍有协调边界。冻结 cohort 的 View 准备和周期检查也仍存在。本轮先把
普通文件最常见的短操作从逐调用协调往返中移出。
