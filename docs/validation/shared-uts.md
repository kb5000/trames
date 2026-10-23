# 共享 UTS namespace 与 worker 名字调用

2026-10-23，第 38 批。机器记录见 [shared-uts-summary.json](shared-uts-summary.json)。

UTS 的实际对象由 Rc 改为 Arc，proc namespace 快照使用弱 Arc，namespace FD
和当前任务持有相同身份。hostname/domainname 在单个对象锁下读取或更新；uname
一次取得两个字段的完整快照，写 guest 内存时已释放锁。配额使用 relaxed 原子
预留/回收，分配 namespace ID 失败会回滚预留；最后引用可在 worker 释放。未把
其他 namespace 对象图或完整 NamespaceHandle 强制声明为 Send。

uname、sethostname、setdomainname 复用同一份 Linux ABI，在当前 worker 直接
执行。权限能力按 epoch 捕获，名字本身保持实时共享；CAP_SYS_ADMIN、长度、
内存故障及 seccomp 顺序保留。unshare/setns、凭据变化、namespace FD 安装仍在
原 task/FD 边界，新的 epoch 捕获新的对象。原名字/版本字段没有改变，CPU/native
普通访存没有加锁、屏障或新检查，也未修改映射页布局。

## 验证

- 8 项 release Rust 检查通过：线程共享名字、unshare 隔离、worker 最后引用释放、
  8 个线程竞争最后一个配额、namespace FD 保留身份、权限与错误地址、seccomp
  和 worker 实时读取。没有失败行为测试被跳过。
- ARM64 双线程 fixture 在内存/磁盘 × 1/2/4 CPU 共 6 组通过。每线程重复设置
  64 字节 hostname/domainname 并读取，检查名字完整、NUL 边界、降权和故障。
- 原 namespaces fixture 在同样 6 组配置通过：fork、CLONE_NEWUTS、unshare、
  setns、旧 FD、进程退出后 namespace 保留、权限，以及原 chroot/exec 检查。
  改动前 runner 跑双核/四核新 fixture 与单核原 fixture，共 3 组通过。
- Release CLI/FFI、严格 release Clippy、格式及差异空白检查通过。

双线程共 1024 次名字修改/查询的内存后端路由对照：

| Guest CPU | 改动前 parallel_rounds | 改动后 parallel_rounds | 改动后 service_requests |
| --- | ---: | ---: | ---: |
| 2 | 978 | 11 | 0 |
| 4 | 935 | 8 | 0 |

这里是相同 ELF 的一次固定工作量对照，调度顺序影响批次数，不能视作整体速度或
CoreMark 加速比。没有更改宿主 hostname 或申请宿主 namespace 权限；不宣称
宿主原生 UTS 对照通过。原始记录在本地 `target/shared-uts-checks/`。

初次 cargo check 发现提取公共 ABI 时 syscall 编号 u64/u32 不匹配，改为已匹配
160…162 范围后的转换，再编译和验证。日志、映射、io_uring、网络和图形未改变，
复用上一批通过结果；未重新运行这些套件，未制作 IPA。仍需继续迁移其他 namespace、
完整 FD/OFD、Node/映射页和 VM/task 生命周期，不能把本批算成完整 namespace 去协调。
