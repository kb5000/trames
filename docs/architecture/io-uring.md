# Guest io_uring

2026-10-22 接入 AArch64 Linux `io_uring_setup(425)`、`io_uring_enter(426)`、
`io_uring_register(427)`；2026-10-23 先扩到 28 个 opcode，随后扩到 30 个，并增加提供缓冲区与 multishot。
它运行于 Trames 的 VFS/网络层，不依赖宿主 Linux io_uring，iPad 使用相同 guest ABI。
目前是兼容实现，尚未覆盖完整 Linux io_uring；2026-10-23 将短请求和完成等待接入 worker，随后迁移固定文件/缓冲、链接/排空；本轮继续迁移提供缓冲、重复完成、计时/取消及 CQ poll。

## 支持范围

| 范围 | 已实现语义 |
| --- | --- |
| setup / mmap | 标准 params/offset、64-byte SQE、16-byte CQE、SINGLE_MMAP、CQSIZE/CLAMP、CLOEXEC |
| 队列 | 私有尺寸/游标、边界与 u32 回绕、release/acquire 发布；无效 SQE index 计 dropped |
| CQ | 溢出完成保留与背压、SQ_CQ_OVERFLOW、NODROP，不覆盖未消费 CQE |
| 数据 | NOP、READ/WRITE、READV/WRITEV、READ_FIXED/WRITE_FIXED、FSYNC/FDATASYNC |
| 文件 | OPENAT/OPENAT2、CLOSE、STATX、FALLOCATE、FTRUNCATE，复用原 VFS 与 open_how/statx 实现 |
| 网络 | SOCKET、BIND、LISTEN、ACCEPT、CONNECT、连接态 SEND/RECV、SHUTDOWN；Unix/IPv4/IPv6 共用原网络实现 |
| 顺序 | IO_LINK / IO_HARDLINK；失败/短 I/O 取消普通链接后继；IO_DRAIN 在后续 enter 中也维持排空屏障 |
| poll | 单次/多次 POLL_ADD、REMOVE、事件/user_data 更新；多次默认按边沿/epoch 去重，可显式 level |
| 超时 | 相对/绝对 TIMEOUT、monotonic/boottime/realtime 选择、完成计数、ETIME_SUCCESS、REMOVE/UPDATE、LINK_TIMEOUT/UPDATE；相对 multishot timeout 支持有限/无限次数 |
| 取消 | ASYNC_CANCEL 按 user_data、原 OFD 或固定文件匹配；ALL / ANY 批量取消，返回取消数量 |
| 缓冲选择 | PROVIDE_BUFFERS/REMOVE_BUFFERS；READ/RECV 的 BUFFER_SELECT、CQE buffer ID；注册/卸载/查询 provided buffer ring |
| 注册 | 固定文件注册/更新/卸载、稀疏 slot；固定缓冲区注册/卸载；eventfd 注册/卸载及 CQ eventfd 禁用位 |
| multishot | ACCEPT、RECV、POLL_ADD、相对 TIMEOUT；MORE 只表示请求仍活动，EOF/错误/取消/退出发布终结 CQE |
| 等待 | GETEVENTS/min_complete、EXT_ARG 相对截止时间、ring FD 的 poll/epoll level/edge/oneshot |
| 生命周期 | 接受请求时保留 OFD/缓冲页；绑定提交 VM/任务；exec/退出取消；关闭/复用数字 FD 不改绑已接受请求 |

`REGISTER_PROBE` 仅公布上表实际实现的 30 个 opcode。普通 I/O 共用原 VFS 的权限、
OFD 位置、存储配额和同步实现。向量表、路径、open_how、sockaddr、timespec 在提交
时导入，避免等待后读到另一份参数；普通缓冲数据在执行时按现有 guest 内存模型访问。
RWF_NOWAIT 与 SEND/RECV 的 MSG_DONTWAIT 遇 EAGAIN 直接完成，其余受支持请求可以
保留到资源就绪。SEND/RECV 暂限连接态，不支持 sendto 的 addr2。

接受新连接、打开相对目录文件时，保留原对象，并将新 FD 安装到原 guest 表。
路径操作的私有视图保留原 `/proc/self/fd` 可见性；字节操作不克隆整张 FD 表。
CONNECT 区分首次连接和等待完成，不在每次推进时重新发起。

## 固定缓冲区与内存边界

`REGISTER_BUFFERS` 真正持有私有匿名 heap/stack 页及其 residency charge。READ_FIXED /
WRITE_FIXED 可以使用注册区间的子区间；越界/无效索引失败。卸载只释放注册表的持有，
已接受请求保留自己的页引用，完成后再释放。页被 unmap、mprotect、mremap、原地址
被另一个映射覆盖时，已接受请求仍访问原物理字节；fork 在子侧分离私有固定页，
CLONE_VM 则继续共享原字节。pin 索引以存储身份为键，避免重映射改变 frame index。

pin 复用现有 SharedPage 的区域访问、LL/SC 监视器和字节版本。为避免硬 MMU arena
局部重用/清零物理 offset，第一次注册 arena 所有的私有页时，将其提升到独立稳定
存储；已有稳定 pin 重复注册不复制。pin 页所在宿主页组暂走软件地址路径，防止
权限/映射调整将其重新安置。unpin 后，后续正常 MMU 准备可以恢复硬件映射。
这项实现不宣称注册零拷贝；数据请求直接使用稳定页所有者，不按原 VA 重新寻址。
普通未注册页的 CPU/原生读写不增加 pin 查询、锁或内存屏障；检查只在注册、fork、
映射/MMU 准备边界发生。pin 页写入同时更新页版本和 decoder backing epoch。
注册时也只刷新所涉及页的 parallel 快照所有权位，包括已提升过的稳定页；独立 pin
排除该页的私有免锁访问，防止注销后重新注册沿用旧标记。其他页仍可使用原快速路径。

当前最多 1024 个缓冲区、合计 64 MiB 注册长度；pin 占用仍受 guest resident budget
约束，unmap 后也不提前退还仍被 pin 持有的额度。文件映射、共享匿名/设备 backing
尚未接入 provider-specific pin/truncate 协议，注册返回 EOPNOTSUPP；不假装固定成功。

## 提供缓冲区与 multishot

经典 PROVIDE_BUFFERS 维护可选的 VA/长度/16-bit buffer ID 队列，REMOVE_BUFFERS 只
移除尚未交给请求的条目。READ/RECV 的 BUFFER_SELECT 在真正 I/O 时选区间；EAGAIN
不消费，其他完成在 CQE 中返回 BUFFER 和 buffer ID。数据地址仍走普通 guest 内存
模型，不因提供而 pin；池受创建 VM 约束，不能把另一个地址空间的同址内存借来完成。
目前 BUFFER_SELECT 不覆盖 READV、RECVMSG 等其他 opcode。

REGISTER_PBUF_RING / UNREGISTER_PBUF_RING / PBUF_STATUS 支持用户分配的标准描述符
ring：4096-byte 对齐、1..32768 个二次幂条目、16-bit tail/head 回绕。内核保留描述符
页并维护私有 head；坏 tail 超过容量时失败，不越界取描述符。tail 和单个描述符用
栈上数组读取，无临时 Vec；joined 边界观察 guest 的发布。描述符 pin 在用户解除
映射/覆盖原地址后仍指向原页，数据缓冲本身仍是按需访问的 VA。MMAP/INC 注册模式
尚未支持，backing 页 pin 限制与固定缓冲相同。

每 ring 最多 256 个组、合计 65536 个经典条目/注册槽。缓冲耗尽以 ENOBUFS 终结
multishot RECV；补充池后需重新提交。正常 stream EOF 终结，datagram 的空消息仍可
产生 MORE。中间 CQE 不释放 IO_LINK/IO_DRAIN 依赖；终结/取消后才释放或取消后继。
取消/退出的 CQE 不携带旧 MORE 或 buffer ID，CQ 溢出保留所有 flags，eventfd 按实际
发布数量通知。普通/硬链接的短传输规则仍沿原实现。

未回收 CQE、溢出 CQE 与活动请求共用原有有界额度，中间完成必须留出活动请求的
终结空间。CQ 达到上限后停住读取/accept，不继续消耗缓冲区或创建 FD；应用消费
完成项后恢复。每次推进最多额外产生 64 个中间完成，并按序号轮转；高序号空闲请求
不阻挡低序号的新就绪事件。多次 poll 复用原 VFS epoch，只在新边沿报告，避免连续
就绪重复产生 CQE；显式 level 模式也受相同背压/批次数量约束。

相对 timeout 和完成计数从依赖/排空条件满足、实际 issue 时开始；未 issue 的 timer
不加入截止时间。TIMEOUT_MULTISHOT 的 off 为次数，0 为无限，每次按导入的相对间隔
重新设置期限；最后一项没有 MORE。绝对 multishot timeout 明确失败。
已背压的 multishot socket/过期 timer 不加入原宿主等待集合，防止立即返回形成空转。
已经报告但保持 level-ready 的 edge poll 由原 joined 检查点重新采样 epoch，存在原
调度周期的检测延迟，不新增轮询线程或消息总线。

## 执行路径与并发

setup/register 与生命周期变更仍由原所有者处理。冻结 cohort 内，worker 可消费 SQ、执行
并发布 CQ：NOP、READ/WRITE、READV/WRITEV、READ_FIXED/WRITE_FIXED、SEND/RECV
和单次/多次 POLL_ADD；PROVIDE/REMOVE_BUFFERS、POLL_REMOVE/UPDATE、TIMEOUT/REMOVE/UPDATE、
LINK_TIMEOUT 与 ASYNC_CANCEL 也在 worker，共 17 个 opcode。同时支持 FIXED_FILE、
IO_LINK/HARDLINK、IO_DRAIN 与 ASYNC hint。支持 pipe/eventfd/timerfd、
已连接 Unix stream/消息资源、无活动防火墙/DNS 的原生 TCP，以及原 FileEpoch 允许的普通
文件读取/覆盖写。TCP POLL_ADD、扩展消息 flags、文件增长/映射冲突/原磁盘服务等
仍交回原路径，不扩大原文件能力的授权。

同一 ring 的 kernel 游标、接受额度和 CQ 溢出使用对象级锁；SQ/CQ 字节使用解释器
原有的 SharedPage 区域访问，不跨线程发送 Rc/RefCell，也不在 worker 中裸拷贝 Mapping。
原所有者保留页 backing 与原 OFD，worker 仅获得 Arc/受限能力。队列锁不包住数据
拷贝、字节资源或文件/socket 操作：不同提交者可以在同一 ring 上并行执行，真正共享
的 OFD 位置/资源由原锁仲裁。已领取但未完成的请求仍计入请求/元数据上限。

固定页及 residency charge 用原 SharedPage/Charge 的 Arc 所有权共享；PinnedBuffer 可
自然满足 Send/Sync，不给整个 Memory、VFS、Rc/RefCell 增加 unsafe Send。注册仍可能
按前述稳定 pin 协议提升一次 arena 私有页；之后跨 worker/epoch 不复制 guest 数据。
WRITE_FIXED 可复用 worker 暂存区读取 pin，不为源缓冲再分配 Vec。新请求按注册表
绑定原 OFD/pin；已接受请求解除注册后仍持有原对象。固定普通文件复用 FileEpoch 的
同一准入、权限、OFD 位置及字节实现，数字 FD 已关闭/复用仍可捕获原普通文件能力。
非支持的固定 slot 仅使使用该 slot 的批次回退，不阻止其他已支持请求。
同一任务的重复 ring FD 共享一张不可变固定资源能力表，原 OFD 也只保留一次；
不同任务分别捕获权限/限制，避免共享队列时混用能力。

链接/硬链接以终结结果及未截断的请求长度判定；短 I/O 或错误取消普通链接，硬链接
继续。向量地址、长度及原请求总量在一次导入中保存，重试不再读取可变 iovec 表。
每 ring 维护活动序号（包括被其他 worker 领取的在途请求）、排空屏障及有界的依赖状态。
DRAIN 等待所有较早活动项，之后的请求等它终结；数据操作仍在 ring 锁外执行。
依赖或排空尚未允许 issue 的请求不订阅 socket/计时资源，只等待 ring 状态通知，避免
未来的就绪 socket/过期 timer 让等待空转。交回 owner 时按序号恢复待完成项与依赖结果，
不丢失普通链接的短传输状态；不承诺跨 enter 拼接未完整提交的链接。

GETEVENTS 不占住宿主执行槽：订阅原资源通知和 CQ 发布通知，重试后 park；原生
TCP 复用已有 epoll/kqueue 事件源，timerfd 使用已有运行队列 deadline。不新增执行器、
服务线程或消息轮询。所有 guest CPU 都已睡眠时，只有真实原生 ring 等待及相关 peer
才继续采用事件驱动 cohort；无关 idle 终端不会把单活跃 CPU 的计算拖入 parallel View。

enter 和 CQ ppoll/epoll 共用 `direct/uring/wait.rs` 的等待订阅管理。停止的调用跨重试
保留订阅；同一 actor 内按原 OFD/读写方向去重，TCP 只在 one-shot 已触发时 rearm，
不重复 dup/注册/删除宿主 FD。不同 actor 保持独立 signal 和 one-shot，不互相覆盖。
每次等待重试前重新计算准入集合，丢弃已取消、已完成或因依赖/CQ 背压失去资格的订阅；
先订阅/rearm 再重试资源，保留原有防丢唤醒协议。cohort 返回时随 PendingIo 释放。
同一 ring 的多个 FD 别名在 CQ poll 中只推进一次，避免重复公平性预算；常见单 ring
不为去重额外分配集合。此次不增加 opcode、注册能力或原有 guest 普通访存检查。

一次批次先导入并检查模式，再产生效果。混合/未拆出的批次不消费 SQ，交回原入口；
已接受但需要原文件/消息服务的请求保留导入元数据与原 OFD。cohort 边界先回收全部
worker/View，再交回队列游标、待完成项及原 GETEVENTS 提交计数，避免重放、重读
向量或因数字 FD 复用改绑。适用的原生等待可在下一 cohort 继续接管，不在原预检查中
先做一遍字节 I/O。epoch 析构也恢复所有者状态。

worker 支持经典/注册提供缓冲及 RECV、POLL_ADD、TIMEOUT multishot；ACCEPT multishot
需要安装新 FD，继续在原入口。EXT_ARG 的相对截止时间只导入一次并随等待交接；
临时信号 mask 仍按原兼容范围返回不支持。REGISTER_PROBE、UNREGISTER_BUFFERS、
UNREGISTER_EVENTFD、PBUF_STATUS 和 UNREGISTER_PBUF_RING 五类注册控制可留在 worker；
新建 ring、注册新 pin、固定文件表/FD/映射及路径/网络控制仍沿原入口。单任务串行入口、cohort 控制
周期、映射/进程边界仍保留；不代表协调器已经完全移除，也没有整体吞吐提速声明。

阻塞 enter 保存已提交数量、原 ring 和一次导入的截止时间。重试不重新消费 SQE、
不因用户修改 timespec 而延长等待，也不因 FD 被复用而改绑 ring。超时返回 ETIME；
若同次 enter 已提交请求，则沿 Linux 的部分提交返回规则返回提交数量。
EXT_ARG 未设置临时信号 mask；非空 mask 明确失败，普通信号仍可中断等待。

提交者使用弱活跃索引；它关闭自己的 ring FD 后，只要另一个进程仍持有该 ring，
请求仍由原上下文推进。最后一个强持有者释放后不保活队列或 FD。普通缓冲的 VM 与
任务身份均匹配才访问，fork 子进程不能用自己的同址内存替父请求完成。
固定缓冲使用注册时保留的页，允许共享 ring 的其他 VM 使用这些固定页。
exec 成功提交前、任务退出时取消请求，失败 exec 不提前取消。

固定文件/请求持有原 OFD。joined 私有 FD 视图以 RAII 恢复原表和身份，异常也恢复；
不执行 guest 回调，不向 worker 发送 Rc/RefCell。不保留 ring/epoll/signalfd/宿主终端
类 FD，避免 RC 环及缺少进程/信号/TTY 权限。ring 自身仍用共享 memfd 直接映射。
FD 表版本缓存弱 ring 索引，无 ring 的程序只检查空缓存，不额外扫描 FD 或加屏障。

## 上限与未支持模式

SQ 上限 4096，CQ 上限 8192；未回收请求/CQE 有界，请求导入元数据上限 1 MiB，
达到限制可部分提交或 EBUSY，已接受请求不丢失。普通单次传输沿用 1 MiB 上限。
链接范围以一次提交批次为界，不承诺跨 enter 拼接不完整链。

尚未支持 SQPOLL/IOPOLL、NO_MMAP、SQE128/CQE32、单发行者/taskrun 模式、
CQE_SKIP_SUCCESS、READ/RECVMSG 等未列出的 multishot 变体、buffer ring MMAP/INC、
SENDMSG/RECVMSG 与 ancillary FD 转移、sendto、zero-copy send、SPLICE/TEE、
目录变更/xattr 等其他未探测 opcode、direct file allocation、personality/restriction/
registered ring、临时 signal mask、EXT_ARG 的 min_wait_usec。
未支持模式返回错误，不按成功空操作处理。

提供缓冲组的选择/消费使用对象内 lease，实际数据访问在 ring 锁外；同组在途请求
占住自己的槽，其余组和无关同 ring 操作仍可推进。EAGAIN/执行前回退不消费槽，
普通错误在完成项携带已选 buffer ID。注册描述符 ring 读取原 SharedPage 的发布，
组绑定原 VM，数据区不被隐式 pin。连接态 Unix datagram 的 ring 能力复用原消息算法，
包含 MSG_TRUNC 的复制/报告长度区别及空消息的 MORE；普通标量 syscall 准入保持原样。

多次完成共享有界额度与 64 次推进预算，序号游标避免高序号饥饿。在途多次请求先
预留额外 CQ 空间；背压、未来依赖和占用的缓冲组不注册会立即触发的宿主资源。
超时/取消只改本 ring 的索引，不接触另一个任务的 guest 字节；已经被其他线程领取
的不可取消 I/O 返回 EALREADY，不假造 ECANCELED 或重放效果。MORE 不释放链接，
终结后才释放依赖。计时解析和提供缓冲算法由 worker/owner 共用。

ring CQ 的 ppoll/epoll 复用原 Poll/WatchState，直接订阅 CQ 和被观察任务的资源，
worker 用自己的 View 推进已接受请求后再检查事件。其他任务保留了 enter 执行上下文
时可只观察其完成；没有保留执行上下文的跨任务后台请求仍回到生命周期检查点，
观察者不会借用别人的 VA。原生 TCP 的 POLL_ADD、普通文件 poll、SCM_RIGHTS，以及
创建/安装 FD、连接、同步/增长/持久化等受限操作仍需后续共享 FD/文件生命周期改造。

带 ring 的任务刚从睡眠/阻塞恢复时，在执行 owner 串行 quantum 前再检查一次并行
条件；无 ring 的计算不增加逐指令检查。真正的 ring host event/截止时间可在全体
CPU 睡眠时继续事件驱动；无关 idle waiter 不影响单活跃 CPU 的 MMU 准入。
新计数 `worker_uring_syscalls` 包含 enter 及上述五类 register 控制；
`worker_poll_syscalls` 计 ppoll、epoll_pwait 和 epoll_pwait2，历史记录的数值保留当时口径。

后续共享 FD/OFD、路径/网络控制及后台请求执行上下文，再衔接异步磁盘与 backing 页 pin。
订阅整理和剩余协调边界见 [等待路径验证](../validation/io-uring-wait-reuse.md)。
最新证据见 [重复完成/计时/CQ 等待迁移](../validation/io-uring-worker-async.md)。
当前证据见 [固定资源与链接迁移](../validation/io-uring-worker-registered.md)，首批 worker 记录见
[worker 验证](../validation/io-uring-worker.md)，兼容范围见
[multishot 验证](../validation/io-uring-multishot.md)；此前的
[扩展验证](../validation/io-uring-extended.md)和[首版记录](../validation/io-uring.md)保留。
