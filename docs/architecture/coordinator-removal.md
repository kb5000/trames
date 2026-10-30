# 协调器拆分进度

2026-10-24 的性能优先级审计见 [整组停顿测量](../validation/cohort-cost.md)：
先让实际 Files/OFD 结构调用在 worker 完成，再缩小 VM 修改的暂停域；
低频查询迁移不代替这两项。仅放宽周期不是生产优化。

截至 2026-10-24，已完成 39 批资源/任务路径迁移，另修复了单活跃进程的 cohort
误准入回退。这个批次数字不代表完成比例：剩余映射和生命周期变更需要更深的
所有权改造，也尚无代表整个产品的调用/CPU 周期占比，不能声称已完成某个百分比。
第 17 批之后统一并复用了 io_uring 等待订阅，未另计迁移批次；第 18 批拆出共享 OFD
可变状态，F_SETFL/FIONBIO 可以在 worker 执行，FD 表结构仍由原所有者维护。
第 19 批进一步共享描述符 CLOEXEC，接入 F_SETFD/纯 CLOEXEC close_range，并处理
fork/CLONE_FILES/unshare/exec 的元数据所有权。第 20 批拆出按 inode 共享的 POSIX
文件锁，F_GETLK/F_SETLK 及关闭/退出释放共用对象，完整迁出路线见 [共享内核状态](shared-kernel-state.md)。

## 发布状态

1.3.0 build 36 包含前 10 批。此后共享 futex/requeue、PTY、单活跃进程 MMU 修复及
第 13 批 TCP 就绪字节 I/O、第 14 批 TCP 宿主事件等待、第 15 批短 io_uring/GETEVENTS、第 16 批固定资源/链接/排空、第 17 批提供缓冲/multishot/计时/取消/CQ poll、第 18 批共享 OFD 状态控制、第 19 批描述符元数据及第 20 批共享文件锁在当前开发代码中，
以及第 21 批共享 inotify/被监视文件 I/O、第 22 批共享 FS 上下文/umask、第 23 批共享信号表/只读查询、第 24 批共享 VM 分配上下文/brk 查询、第 25 批共享 flock/OFD 锁寿命、第 26 批阻塞 flock 通知等待、第 27 批共享关闭通知生命周期、第 28 批共享 inotify 实例/注册索引、第 29 批共享原生 socket 端点/等待租约、第 30 批共享 PTY 从端寿命、第 31 批共享可写映射引用、第 32 批共享 Unix 路径绑定身份、第 33 批共享 FIFO 身份/元数据分离、第 34 批共享映射长度/变更状态及脏页元数据去重、第 35 批共享 OOM 状态与 BPF 程序寿命、第 36 批共享系统日志与非阻塞 syslog、第 37 批日志阻塞通知与 kmsg 直接 I/O、第 38 批共享 UTS/名字调用、第 39 批共享 PID namespace 身份与映射已做各自受影响验证，尚未进入新 IPA。
Linux 的结构/性能数据不能代替 Apple ARM64 与 iPad 验收。

## 当前路径

| 范围 | 已实现 | 仍需协调角色的部分 |
| --- | --- | --- |
| CPU / musl 原生函数 | 当前 worker 执行，共用 guest 内存模型 | 需要 backing checkpoint 的受限 NativeCall |
| 常用查询与 FD/OFD 标志 | 身份、时钟、getcpu、F_GETFD/F_GETFL、F_SETFD/F_SETFL/FIONBIO、纯 CLOEXEC close_range | FD 表结构仍冻结；动态 proc/sysfs 完整进程状态、可变权限 |
| 系统日志 | syslog 全部操作共用 ABI/epoch 权限；阻塞流和 kmsg I/O、seek、poll/epoll 由共享资源通知恢复；游标按实际打开对象共享 | FD 安装/关闭/dup、epoll 注册及任务/凭据生命周期提交 |
| UTS namespace | 共享实际身份/名字，原子配额；uname、sethostname/setdomainname 在 worker 复用 ABI | unshare/setns、凭据及 namespace FD 安装，其他 namespace 对象图 |
| 共享运行队列 | 原任务/View 保留；等待让出执行槽；直接通知恢复；无生产回复通道等待 | cohort 准备、统一停止、映射提交；少数 legacy 唤醒 |
| pipe/eventfd/timerfd/Unix stream | 标量/向量、字节消息、适用的 ppoll/epoll 与 deadline | FD 安装/表项变更、SCM_RIGHTS、信号、临时 signal mask |
| inotify | 共享实例寿命/注册索引及队列、read/readv/FIONREAD、poll/epoll、rm_watch；普通文件 worker 直接产生 ACCESS/MODIFY；共享关闭租约最后释放可发 IN_CLOSE，链接状态与监视对象已共享 | 创建/路径注册、实际 FD/OFD 关闭和 inode/目录结构变更 |
| FS 上下文 | CLONE_FS 共用目录/mount 引用对象，无逐任务传播；umask 共享原子在 worker 交换 | 路径/inode/mount 仍由 owner 操作；setns/pivot_root 关系检查仍需协调 |
| 信号处理函数表 | CLONE_SIGHAND 共用对象；fork/exec 按域复制/私有化，无逐任务数组传播；查询型 rt_sigaction 在 worker | 处理函数修改的 pending 清理、作业控制、信号生成/投递、mask 和任务退出 |
| VM 分配上下文 | CLONE_VM 直接共用 brk/mmap 游标；删除快照比较和逐任务传播；brk(0) 在 worker 原子读取 | 游标修改仍需 VM 映射提交；映射表、权限及 backing 生命周期仍冻结在 cohort 内 |
| futex | private/shared wait/wake、位集、requeue/cmp_requeue | 未保留 View 的外部 waiter、PI/受限操作 |
| PTY | master/slave 字节、向量、只读属性/尺寸、poll/epoll；实际从端租约最后释放可在 worker 更新计数、通知 HUP/EIO | 主端关闭/devpts、信号输入、作业控制、其余可变 ioctl、宿主 UI pump |
| 普通文件 | 合格文件的 lseek/fstat、读取与受限覆盖写、F_GETLK/F_SETLK、flock/解锁及基于原始 OFD 的阻塞通知等待；大块读取异步交还执行槽 | 文件增长/持久化/路径及权限元数据、映射冲突等服务；阻塞记录锁、FD/OFD 结构寿命及死锁检测 |
| Vulkan | worker 内资源/命令/提交/传输；context 与编译等待直接交接 | 设备 teardown、部分 FD 认证/native relay；同 context 对象图仍独占 |
| TCP | 已连接、无规则的原生 TCP read/write/readv/writev；阻塞交还执行槽，宿主 epoll/kqueue 事件直接恢复，保留 socket deadline；实际宿主端点/配额与边沿观察器共享，等待持有同一寿命租约 | 消息/控制/连接调用、网络 poll/epoll、过滤与虚拟栈；单任务串行入口仍用原网络等待 |
| UDP / DNS / 网络控制 | 原功能继续工作 | 当前仍在原网络所有者路径 |
| io_uring | 30-opcode 兼容实现；17 个 opcode 的字节、固定/提供缓冲、重复完成、链接/排空、计时/取消及 GETEVENTS/EXT_ARG 可留在 worker；五类 register 查询/卸载；CQ ppoll/epoll 直接等待 | setup、新 pin/文件注册、FD/路径/网络控制、受限文件服务及未保留执行上下文的跨任务后台请求；见 [设计](io-uring.md) |
| FD、映射、进程与命名空间变更 | OFD 状态、每描述符 CLOEXEC 共享原子字段；fork/共享/脱离元数据域；原功能继续工作 | FD 创建/安装/关闭/dup、映射及生命周期多数仍需整组 worker 返回 |

## 是否已经完全撤掉

没有。`workers::Event::Service` / `Request::for_epoch` 仍用于把受限文件等服务交给
所有者；这条生产路径保留原 Job/View，通过运行队列恢复，已经没有逐调用 reply
channel，但仍有消息转交及所有者调度成本。未准入 syscall / NativeCall、FD/映射/
进程变更仍可让整个 cohort 返回。不能把“没有新服务线程”或测试的 service_requests=0
理解为整个协调角色已经消失。

目标是普通工作直接运行、必要同步限定到实际共享对象；同一套资源语义由串行入口
和 worker 复用。单 CPU 的直接 MMU 路径仍有价值，调度/退出/映射提交的同步职责也
必须存在，但它们无需长期集中在一个所有者。删除原入口前需要满足以下条件：

| 剩余依赖 | 原因 | 要继续替换的所有权边界 |
| --- | --- | --- |
| FD 安装/关闭、固定文件表、ACCEPT/SOCKET/CLOSE | VFS/FD 表和 namespace 权限仍由原所有者维护 | 共享 FD 表 + Arc OFD，按表/对象更新；保留代际与最后关闭语义 |
| OPENAT/STATX、文件增长/同步/持久化 | 路径、权限、backing 与配额生命周期未完全共享 | 拆分路径与文件对象，不把完整 VFS 套进全局锁；复用原文件算法 |
| CONNECT、网络控制、native TCP POLL_ADD | 网络 namespace/规则和边沿状态仍有所有者数据 | 共享 socket 控制/边沿事件状态，沿用已有宿主 reactor；不能反复订阅 level-ready socket 模拟边沿 |
| 无 enter actor 的跨任务后台请求 | CQ 观察者只有自己的 View，不能借自己的同址 VA 执行他人请求 | 为原提交 VM 保留可调度执行上下文与取消/退出归属 |
| mmap/exec/exit、namespace 改动 | 映射与权限快照需一致提交 | 按相关 VM/FD 表/线程组停止和提交，让无关任务继续运行 |

io_uring 的 owner/worker 引擎还没有合并；计时、缓冲与 probe 已共享，这次将 enter/CQ
poll 的资源等待也抽到 `direct/uring/wait.rs`。继续新增重复的控制/文件实现会增加维护
成本，应先建立上述共享所有权，再统一执行逻辑，而非以更多 worker opcode 作为终点。

第 18 批已将 OFD 的不可变访问位与共享可变状态分开：F_SETFL 使用一次 relaxed store，
FIONBIO 使用单字段原子位操作；pipe/TCP 的阻塞判断和文件 append 准入读取同一状态。
owner 与 worker 共用设置算法，无全局 revision 失效、资源锁或额外普通访存屏障。
已有阻塞字节调用在下一次重试时也读取新状态；设置标志本身不制造资源唤醒，poll
等待不受 O_NONBLOCK 影响。FD 表/CLOEXEC、OFD 本体及 namespace 权限尚未迁移。
结果和固定工作量对照见 [共享 OFD 状态](../validation/worker-fd-status.md)。
上述为第 18 批边界；第 19 批已迁出 CLOEXEC 元数据，结构 map 与 OFD 本体仍待后续。
元数据域与结构缓存身份独立，fork 不重复准备相同 OFD 文件/字节能力；共享同一 Files
域的任务直接观察标志，成功 exec/UNSHARE 脱离，失败 exec 保留共享关系。
证据见 [描述符元数据](../validation/worker-fd-table.md)。

## 后续顺序

1. 优先共享 FD/OFD 与文件对象，按对象处理可独立的安装、关闭及元数据变化；保留
   generation/权限/生命周期边界，避免把整棵 VFS 放到一个全局锁下。
2. 在已有宿主事件接入基础上扩展 UDP、消息 ABI、网络 poll/epoll 与连接控制，
   并让串行入口复用同一资源/等待实现；为后台请求保留原 VM 执行上下文。
   不新建资源服务线程或周期轮询。
3. 将映射和进程变更的暂停范围缩小到相关 VM/FD 表/线程组，使不相关 guest 工作
   继续执行。这是撤掉全体协调依赖的主要剩余架构工作。

单活跃 CPU 的计算继续使用串行 Memory 直接 MMU，不因无关 idle waiter 强迫进入
parallel View；真正相关的等待与多活跃任务保持交接/并行。无 JIT、无额外普通访存
屏障；原算法的线程安全和 ARM 内存模型责任不变。

详细边界与历史批次见 [epoch services](epoch-services.md)。最近的证据：
[共享 futex](../validation/worker-futex.md)、[PTY](../validation/worker-pty.md)、
[CoreMark 回退修复](../validation/coremark-build35-36.md)、[TCP](../validation/worker-tcp.md)、
[TCP 事件等待](../validation/worker-tcp-waits.md)、[io_uring worker](../validation/io-uring-worker.md)、[固定资源/链接](../validation/io-uring-worker-registered.md)、[重复完成/计时/CQ 等待](../validation/io-uring-worker-async.md)。
等待订阅复用、别名去重和剩余依赖审计见 [等待路径整理](../validation/io-uring-wait-reuse.md)。

第 20 批已把 POSIX 范围集合移到 inode 的共享对象，冲突检测/提交原子完成；owner 与
worker 共用算法/ABI，不再修改 Filesystem 中的 owner 锁表。关闭释放使用同一对象，
进程退出从弱引用注册表逐 inode 清理。F_SETLKW 等待/死锁检测及 FD 表本身仍待迁移。
576 次定向控制操作的 cohort 从 577 降到 1；见 [文件锁验证](../validation/worker-record-locks.md)。

第 20 批之后继续拆分了实际 pipe 端点寿命与 owner inode 投影：Arc 端点最后引用可在
worker 释放并直接产生 EOF/HUP/EPIPE，原生剪贴板 relay 保留独立元数据 anchor。
本次没有新增 worker syscall，不另计批次；FD 表、完整 OFD 和 io_uring owner 保留
引用还需继续迁移，见 [共享管道端点](../validation/shared-pipe-endpoint.md)。

随后 CLONE_FILES 改为真正共用一个 owner 表，删除结构修改后的逐任务 map 传播、
每次 syscall 的 FD 版本跟踪和 Process.files_id。退出/exec 依据表引用释放，lookup
保留稳定 OFD，dup3 先替换再关闭旧目标；共享 clone 不再分配临时私有表。
327 项受影响 Rust 检查及 1/2/4 CPU 的 FD/锁/FIFO/io_uring 验证通过。
close/dup 仍需 join，OFD 和 worker 动态查表尚未迁完；不另计 worker syscall 批次。
见 [共享 Files 表](../validation/shared-files-table.md)。

第 21 批把 inotify 队列与 watch 消费移出 owner，普通文件 worker 可直接发通知，
read/poll 等待和 rm_watch 不再转交。实例锁之外唤醒，不增加服务线程；路径注册仍冻结。
64 轮通知交接在双核/四核均完成 64 次 worker 文件调用、32 次 poll 和 1 次撤销，
服务请求降为 0。实际文件表/OFD 生命周期仍待迁移，见 [inotify 验证](../validation/worker-inotify.md)。

第 22 批删除每次 syscall 的 FS 快照比较、ResourceChanges.fs、Process.fs_id 和目录
上下文传播；umask 以对象内原子交换直接执行。普通 exec 按 Linux 行为保留 CLONE_FS，
同时保持文件表私有化。256 次交换的双核/四核并行批次约 258 → 3–4，详见
[共享 FS 上下文](../validation/shared-fs-context.md)。在该批次结束时 heap/actions 及路径/生命周期边界仍待迁移。

第 23 批进一步删除 actions_id、处理函数数组传播与通用 ResourceChanges，CLONE_SIGHAND
直接共用信号表；fork/exec/reset 保持各自共享域。查询型 rt_sigaction 在 worker 执行，
512 次查询的双核/四核批次约 513 → 3–4。heap 传播、pending/作业控制与其他 task
生命周期仍需继续迁移，见 [共享信号表](../validation/shared-signal-actions.md)。

第 24 批按实际 VM 身份共享分配上下文，删除 heap 快照/比较及最后的逐任务传播。
brk(0) 在 worker 用共享原子读取，512 次查询的双核/四核批次约 514 → 3–4；改变映射
的调用仍保留 VM 提交边界。原 Files/FS/actions/heap 传播至此全部移除，整组停止、
映射与生命周期协调还在，见 [共享 VM 分配上下文](../validation/shared-vm-allocation.md)。

第 25 批删除文件系统级 flock 表/失效 OFD 扫描，改为 inode 对象和 OFD 共享锁租约，
最后租约可在 worker 直接释放/通知。合格文件的非阻塞操作与解锁进入 worker；
640 次控制的双核/四核并行批次 643 → 3–4，单核未见改善。该批尚未涵盖完整 OFD/FD 结构和阻塞
flock（阻塞等待后续见第 26 批），见 [flock 验证](../validation/shared-flock.md)。

第 26 批将阻塞 flock 接入共享运行队列，冲突让出执行槽，inode 解锁直接唤醒；
停止/重入保存原始请求，join 在处理任何 close/dup 前保留完整 OFD，避免 FD 重用
导致锁错文件。信号中断/重启、停止、退出按原有任务语义清理。普通无竞争的单 CPU
调用不额外构造等待请求。原始 OFD 结构/最终析构仍受 joined 生命周期限制，
没有据此宣称 close/dup 已迁出。256 次控制的多核协调批次约 132 → 3–4，
详见 [阻塞 flock 验证](../validation/blocking-flock.md)。

第 27 批将文件关闭通知从 owner inode/路径 Rc 中拆出，OFD 持有 Arc<FileClose>，
监视目标按 inode 共享，目录链接的 parent/name/linked 按对象同步。最后租约可在
worker 直接产生 IN_CLOSE；实时注册、rename、EXCL_UNLINK 和关闭后删除事件顺序
都复用同一组通知实现。普通字节能力不延长租约，未监视/未使用的 inode 不分配通知对象。
实际 close/dup 和部分端点仍未迁出，此批是生命周期依赖拆分，不增加 worker opcode
或声称整体加速，见 [共享关闭通知验证](../validation/shared-close-notifications.md)。

第 28 批把 inotify 实例由 Rc/RefCell 变为 Arc 对象，注册索引仅持有共享通知目标的
弱引用，不再依赖 Node。worker rm_watch 共用完整撤销逻辑，立即清理索引与标记；
普通字节快照不保留实际实例，最后实例租约可在 worker 清掉监视。一次性 watch 的
存在检查与更新合成同一队列事务，避免复活已发 IGNORED 的旧编号。路径解析、安装
FD 和完整 OFD 仍未迁出，见 [共享 inotify 实例验证](../validation/shared-inotify-instance.md)。

第 29 批把原生宿主端点、配额租约和持久边沿观察器从 socket 的 Rc 控制投影拆出。
创建/accept 在宿主调用前原子预留，失败自动回收；最后实际端点引用可在 worker
关闭宿主 FD 并回收配额。普通 DirectTcp 仍为 Weak；普通阻塞 I/O 和 io_uring
的宿主等待注册持有真实端点租约，取消时先注销/关 duplicate FD，再在注册锁外
释放资源，无额外堆包装。namespace/规则/连接控制、完整 OFD、动态 FD 表及其
join 仍待迁移，不据此新增 worker opcode 或声称整体提速。
见 [共享原生端点验证](../validation/shared-native-endpoint.md)。

第 30 批把 PTY 从端实际打开引用拆为 Arc<PtySlaveEndpoint>；dup/fork 和进行中的
引用保持同一打开计数，独立 open 才增加。最后 worker 租约释放直接原子递减并
通知主端，无 inode/mount/terminal 锁，缓冲输出仍可读完后返回 EIO。字节能力
不保留从端引用。主端删除 devpts 节点及作业控制仍依赖 owner，需要和目录/任务
域继续迁移，本批不移除这些 join；见 [从端验证](../validation/shared-pty-slave.md)。

第 31 批删除 mapped inode 的逐页 writer 扫描和 writers_cached 失效，实际
FileMapping 持有共享计数租约，按 inode 与具体 mount 分别计数。最后引用可在
worker 释放，owner 查询立即看到；只读 remount、普通 umount 与懒卸载树回收
都检查同一引用状态。普通映射先完成回写尝试再释放租约，字节/MMU 路径不变。
这只迁移可写引用及忙状态；FilePage、Node、映射表、回写提交与主端 devpts
生命周期仍要继续拆分，见 [映射引用验证](../validation/shared-mapping-writers.md)。

第 32 批把 inode 的 Unix socket 引用换成共享弱身份，消除到 LocalSocket Rc
控制图的直接依赖。关闭在资源通知前立即使身份失效；插入失败/关闭清理原所有者
的弱解析索引。索引按 guest 共享，保留跨网络 namespace 的 pathname 访问与
abstract 名称隔离；硬链接、overlay、磁盘恢复和 Wayland/X11 清理使用相同语义。
这仍是 inode 共享化的前置拆分，完整 FD、Unix listener/rights 和连接控制未迁出；
字节 I/O 热路径不变，见 [绑定身份验证](../validation/shared-unix-binding.md)。

第 33 批删除 Rc<RefCell<Pipe>>，FIFO inode 直接持有稳定编号与共享资源；匿名
pipe 元数据移到实际打开描述符和原生 relay 的 owner 投影。资源不反向引用 Node，
不保活已删除 inode；splice 以共享资源身份判断别名。少一次管道包装分配，原
字节/握手/关闭行为不变；同时补齐既有 FIFO 硬链接误报 EPERM，含恢复后的共享
inode 与通道检查。完整 FD/OFD 与路径控制未迁出，见 [FIFO 验证](../validation/shared-fifo-identity.md)。

第 33 批之后试验了 Arc FilePage + 布局 RwLock，功能验证通过，但单核映射读写
微基准耗时增加约 26%–27%，已撤回，未计入迁移批次。下一步先解决串行映射的
稳定页能力及失效，再共享 inode；不能把协调成本转为逐次访存锁。见
[未采用的映射页原型](../validation/shared-file-page-experiment.md)。

第 34 批先拆开映射检查与 inode：MappingState 按文件共享稳定身份/原子变更世代，
普通文件和 memfd 的 live EOF 直接读取实际存储能力，无长度快照和 inode 借用。
overlay 下层及会整体刷新内容的伪节点继续沿原 owner 检查。修改时间在页面首次
变脏时更新，后续 CPU/native 写入保留逐次 page/backing 失效；成功回写/截断重置
脏页状态。这移除了热循环的 inode 元数据和时钟操作，未加布局锁、CPU 普通访存
屏障或新线程。固定单核 mmap 微基准耗时约降 19%–20%，不代表整体/iPad 速度。
完整 FilePage、首次脏写元数据、Node/Lower、FD/OFD、VM/task 仍待迁出；没有新增
worker syscall 或删除映射提交 join，见 [映射状态验证](../validation/shared-mapped-state.md)。

第 35 批拆掉 OFD 中 OOM/BPF 两条 owner Rc 依赖。OOM 的任务弱身份改为共享
弱引用，分数与权限下限打包成单个 32-bit 原子；非特权写入在 CAS 提交时重新
检查下限，fork 取同一原子快照，线程共享偏好但保持独立任务寿命。BPF 已验证
指令、FD 和 cgroup 附加使用同一 Arc 程序，弱注册表不保活，最后 worker 引用
释放后可立即回收配额。程序执行没有新增锁；完整进程/proc 权限与 cgroup/FD
安装仍待迁移，不增加 worker syscall，见 [验证](../validation/shared-ofd-leaves.md)。

第 36 批把会话日志从 Rc/RefCell 拆为共享资源，非阻塞 syslog 在 worker 共用
原 Linux ABI，日志事务只锁自身，写 guest 内存时已释放。权限按 epoch 捕获，
seccomp 与输出故障保留原语义；action 2 阻塞读取及 kmsg FD 游标仍走原路径。
768 次双线程查询的内存后端双核/四核并行批次 406 → 10，未增加协调服务请求。
这不是完整 FD/等待迁移或整体加速比，见 [系统日志验证](../validation/shared-kernel-log.md)。

第 37 批把 syslog 权限能力并入 DirectSyscalls，legacy stream 复用 PendingIo 和
共享运行队列，append 解锁后直接唤醒；kmsg 游标按实际打开对象共享，字节 I/O、
seek、poll/epoll 共用同一资源。补齐 kmsg 的 epoll 准入、ET 世代和最后关闭失效，
无写入者的日志等待不改变单活跃 CPU 路径。单宿主槽验证无重放/服务请求；共同
支持的三类等待双核路由批次 55 → 17，不代表整体速度。完整 FD 生命周期仍待
迁移，见 [日志等待验证](../validation/shared-log-waits.md)。

第 38 批把 UTS 对象及弱快照改为 Arc/Weak，名字按对象同步、配额原子预留与
回收，最后引用可在 worker 释放。uname、sethostname/setdomainname 共用 ABI
和 epoch 权限直接执行；unshare/setns 与其他 namespace 仍保留任务边界。
1024 次双线程名字操作的双核/四核并行批次分别 978 → 11、935 → 8，不代表
产品整体速度，见 [UTS 验证](../validation/shared-uts.md)。

第 39 批将 PID namespace 身份、弱注册与嵌套映射迁为实际共享对象；默认空间
转换不取得映射/注册表锁，祖先分配按根到叶完整提交，死亡/上限失败不消耗编号。
任务生命周期及裁剪仍在原提交边界；本批没有新增 worker opcode 或整体提速承诺，
见 [PID namespace 验证](../validation/shared-pid-namespace.md)。


## Shared Mac/iPad follow-up (2026-10-30)

Native display connections no longer force every CPU to return on a control
check. The existing owner pumps Wayland/XWM bytes and clipboard resource relays
while workers retain their Views. Active and already-idle native cohorts use
the existing resource wakeups and bounded host-control wait.

Sandboxed mapping calls also enter the common mm-local checkpoint path. The
owner evaluates the original seccomp entry once; ALLOW executes only after its
related address-space Views quiesce. Other actions retain the full joined
boundary and carry their exact-entry result to the ordinary handler. An entry
or immutable filter-chain change invalidates the cached result. This keeps
Firefox's sandbox enabled and preserves the same iPad kernel implementation.

See [the memory contract](../../runtime/docs/architecture/region-memory.md#filtered-memory-calls)
for admission, cached-decision identity and negative-control coverage. Mac's
`--metrics` reports pending call occurrences at whole-cohort joins; these are
not unique first-stop causes or wall-time percentages. The Mac navigation
validation records the intermediate display-only result as well as the final
combined result, including failed/error observations and sampling exclusions.
