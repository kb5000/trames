# FD 查询、映射读取与 Unix stream 成组优化

2026-10-20，基线 `50b98c33517539c83be0c4175bbc7da20f3336a7`。
这次将五项相关改动合在一批，使用同一组工作量验证；[架构说明](../architecture/epoch-services.md)
描述当前边界，[证据清单](epoch-batch/manifest.json)记录最终源码、二进制和原始数据摘要。
没有生成 IPA，也没有新的 iPad/CoreMark 跑分。

## 改动

1. F_GETFD/F_GETFL 在 worker 内读不可变 FD flags 快照，使用同一个 Linux flags ABI
   转换函数；不再逐调用请求协调器。FD 表/CLOEXEC 修改使缓存失效，F_SETFL 的 revision
   同时处理共享 open description、独立 fork FD 表。filtered/trace 调用继续走原路径。
2. 每 worker 复用回复 channel，唯一 sender 随请求发出并随结果返回；意外丢弃请求可
   断开等待，正常取消仍 drain。保持 View 所有权、CPU/registry 驻留和已完成操作不重放。
   请求 Box 仍有分配，没有宣称全路径零分配。
3. 无共享写入能力的 readonly/private 映射允许文件读取；private COW 不改变 inode。
   文件写入仍要求无活跃映射，潜在 MAP_SHARED writer 仍 join。writer 判断按映射创建、
   next_page、释放失效并缓存，避免每次读取扫描整个文件映射页表。
4. guest AF_UNIX SOCK_STREAM 的就绪 read/write/readv/writev 接入服务。阻塞 EAGAIN、
   SIGPIPE、recvmsg/FD 安装、native bridge、PTY job control、AF_INET 保留原边界。
5. Kernel 复用一份写入 scratch Vec，标量/向量/定位服务继续调用同一套 Linux helper。
   每次按本次长度完整填入有效字节才输出；没有为每进程保留大块缓冲，也没有改变原生
   算法的锁或 guest 访存协议。

同时合并服务准入的重复 FD 查找：按保留的 open description 一次分类 stream/file，
无需先克隆路径、再分别查找流和文件。VFS 请求仍由一个协调器依次处理；宿主磁盘 I/O
尚未异步化，不能称整个 Linux 或 Vulkan 已完全并行。

## 正确性与覆盖

新增验证覆盖 fork 后共享 status flags/独立 CLOEXEC、快照刷新与 EBADF、flags 查询
在本地完成而修改命令继续 checkpoint、128 次连续请求/View 返回、丢弃请求不会卡住
worker、private COW 写入不污染文件、跨 VM 共享 writer/overlay 的准入失效、Unix stream
就绪/阻塞/非阻塞/SIGPIPE、缓冲区缩短与 EFAULT 不输出旧尾部。

本地 Core76、VFS159、Linux23 全模块通过；新增 scratch 用例补跑后 Linux 共24项。
后续映射缓存与准入合并只重跑受影响的 mapping/overlay/service 用例，复用未变通过结果。
五个受影响 crate 严格 Clippy、最终生产三终端 PTY/共享文件/cwd/env/resize/Ctrl+C/
背压/关闭与取消检查通过。CPU/MMU/GPU/UI 与原生函数算法实现未改，相关旧验收复用。

同一个静态原版 musl fixture 在 metadata、pipe、file、vector、unix、mapped-read、
mapped-shared 七种模式各执行 1/2/4 CPU，共21组合。与本机 C 输出一致，检查所有原生
函数/字节计数及无 fault/reject/fallback。mapped-shared 写后读取仍 join，读取最新
字节；没有把一致性回退删掉来获取速度。Apple ARM64/16 KiB 页对应结果以关联 Actions
为准；本机 C 是输出语义 oracle，不是新的完整 CPU 差分验收。

## 固定工作量对照

Linux x86_64、4 KiB 宿主页，release，4 guest CPU、两个 pthread。
每次4096次1 MiB原生填充（4 GiB），另一个线程完成32768轮目标 I/O；无耗时自适应循环。
相同 guest ELF/输入、同一原版 musl 和 NativeCall stub，数据输出和原生字节数全部校验。

| 混合负载 | 基线两次 | 最终候选两次 |
| --- | ---: | ---: |
| metadata | 0.256 / 0.259 s | 0.309 / 0.249 s |
| file | 0.432 / 0.432 s | 0.468 / 0.438 s |
| vector | 0.437 / 0.462 s | 0.449 / 0.445 s |
| Unix stream | 2.155 / 2.856 s | 0.188 / 0.171 s |
| private 映射文件读取 | 0.764 / 1.108 s | 0.119 / 0.116 s |

收益明确集中在新覆盖的 Unix stream 与 private 映射读取；这是 peer CPU/native 不再被
这些 I/O 反复打断的混合负载收益，不是 memcpy 本体、应用整体或 iPad 的提升预测。
FD 本地查询、回复 channel 和 scratch 已减少往返/分配，未从本表证明各项独立提速。
metadata/file/vector 的短样本有波动，file 有一次0.468s，不作“所有场景均加速”的结论。

初轮按 before/after、after/before 成对各两次。随后补映射判断缓存和一次 FD 准入查找，
最终候选重新构建并按同一输入执行两次，复用精确冻结基线；最终行不是新交错的负载控制。
中间候选保留在 prototypes 并排除出最终表，不挑最小值。构建/其他测试不与计时并行。
Unix 总 syscall 为基线65693、候选65691，pthread 启退同步会受调度影响，此处未按号码
归因；固定的32768轮数据 I/O 没有减少，其他模式 syscall 总数一致。
[完整计数和 stdout/stderr](epoch-batch/mixed/results.json)。

## Docker 生产会话

与前轮同一个冻结1741-inode store、缓存 default-probe、原版默认 dockerd/托管
containerd/runc/overlay2，4 CPU/4 GiB，交互 TTY 与第二终端轮询。不含下载，不是用户
完整 rootfs 或确切 alpine:latest digest；没有恢复 Go/nofile/runtime 特殊配置。

| 样本 | daemon ready | run 到 shell 提示符 | exit/--rm |
| --- | ---: | ---: | ---: |
| 原基线 | 10.126 s | 23.328 s | 1.976 s |
| 最终候选 | 11.260 s | 22.519 s | 1.947 s |

中间两版分别23.008、20.977s也保留。**没有证明稳定 Docker 提速**，此前20–23秒范围
仍有重叠；未以中间较低值作最终结果，iPad 十几分钟问题仍待设备实测。

下一阶段仍是可写共享映射的 backing 一致性、真正的宿主 I/O 提交/完成、网络/PTY、
动态 proc/futex/进程生命周期与 Vulkan 资源范围同步。本轮保留这些边界。
