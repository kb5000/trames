# 日志资源的直接 I/O 与通知等待

2026-10-23，第 37 批，接续第 36 批的共享日志。
机器记录见 [shared-log-waits-summary.json](shared-log-waits-summary.json)。

## 实现边界

syslog 的 epoch 权限能力并入 DirectSyscalls，不再在 Snapshot 里保留另一条
调用分支。全部 syslog 操作共用同一 ABI；legacy READ 无数据时复用 PendingIo、
Readiness 和运行队列，交还宿主执行槽。订阅后再次读取决定是否挂起，append
在释放日志锁后直接通知；取消/停止释放订阅，重入保留原调用，不重新执行 SVC。
日志调用的 action 不是 FD，不从同编号 socket/timer 继承超时或分类指标。

`/dev/kmsg` 的游标从 owner OFD position 拆为共享 Kmsg 对象：dup/fork 共用，
独立 open 各自读取。游标、越界恢复与 seek 的事务复用日志锁，单字段存储为
relaxed 原子，不再增加第二把游标锁。proc fdinfo 读取实际共享游标。
read/write/readv/writev、lseek、ppoll 及 epoll 字节/就绪能力由当前 worker 执行；
owner 使用同一实现。此资源没有依赖 Node 的最后关闭效果，完整 FD 安装、关闭、
dup 及 epoll 注册控制仍在原生命周期边界。

补齐此前被普通文件过滤规则拒绝的 kmsg epoll 准入。每次新日志和游标 seek
提供事件世代，owner/worker 共用 ET 状态，未排空时的新记录仍可触发新边沿。
最后 OFD 关闭使其他 FD 表的缓存失效，旧弱 watch 不会被字节能力意外保活。
记录读取前的输出检查、独立读取/清空、限流和每条写入的大小限制保持原行为。

只有实际可写 kmsg 描述符能为无 FD 的 legacy syslog waiter 提供并行调度依据。
单纯共享会话日志，不会把无关的单活跃计算移出串行 MMU 路径。未增加轮询、
协调/服务线程，CPU/native 普通访存没有改动。

## 验证

- 14 项不同的 release Rust 检查通过。新检查覆盖多线程共享/独立游标、owner 与
  worker 的 ET 一致性、seek、最后关闭、通知回调不持日志锁、取消订阅、输出故障；
  单宿主槽确定性验证 kmsg 和 legacy stream 各挂起/恢复一次，原两任务共执行
  6 条指令、完成 2 个直接调用，零协调服务请求且无 SVC 重放。
- 调度检查确认无写入者的日志等待不改变单活跃执行选择，有实际写入者才准入。
  原有 unrelated I/O 与共享资源单槽检查通过。
- ARM64 guest fixture 的 4 类等待（legacy stream、kmsg read、poll、ET epoll）
  在内存/磁盘 × 1/2/4 CPU 共 6 组通过，每类 8 次 fork 进程间交接。
- 单独运行相同 ELF 的原有 3 类操作（no-epoll），新旧 runner 各 1 组，双核内存
  parallel_rounds 为 **55 → 17**。这是路由次数对照，不能推算产品加速比。
- 第 36 批查询/权限 fixture 和原 compatibility-log 的日志子集各在内存 2 CPU、
  磁盘 4 CPU 通过，共 4 组。复用已有 ELF/BusyBox/musl，不重编或运行无关文件 ABI。
- Release CLI/FFI、严格 release Clippy、格式和空白检查通过。

合计 11 组新 runner guest 验证、1 组通过的旧 runner 对照。旧 runner 初次运行
完整 fixture 时在 epoll_ctl 拒绝 kmsg，这正是本批补齐的原有缺口；不是把失败
计作通过，随后用显式 no-epoll 模式对照共同支持的操作。三项新 Rust 测试初次
因测试 VFS 尚未初始化 `/dev/kmsg` 返回 ENOENT，补齐与现有测试一致的 runtime
初始化后通过；后续仅补跑失败项、新增项和受影响边界，已通过的不相关项复用。

原始记录保存在本地 `target/shared-log-wait-checks/`。没有读取宿主内核日志或
更改宿主权限，没有重跑 CoreMark、Docker、CPU 或图形套件，没有构建 IPA。
完整 FD/OFD、Node/映射页、VM/task 生命周期与全体暂停仍未全部迁出。
