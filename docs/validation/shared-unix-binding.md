# Unix socket 路径绑定身份

2026-10-23，第 32 批协调器拆分。inode 的 `Content::Socket` 不再包含
`std::rc::Weak<LocalSocket>`，改为可共享的弱身份。完整控制对象仍由原所有者管理；
这是继续共享 inode/FD 所有权的前置拆分，没有新增 worker syscall 准入。
机器记录见 [shared-unix-binding-summary.json](shared-unix-binding-summary.json)。

## 所有权

只有 pathname bind 才分配共享身份，abstract/socketpair 不额外分配。inode、硬链接
和 overlay 别名持有 Weak，不延长实际 socket 寿命。身份包含一个 relaxed 活跃位；
读者暂时保留其 Arc 时，最后 socket 关闭仍会先使身份失效，再释放 channel/rights。
活跃位不承担字节发布或 socket 控制同步的职责。

原所有者的解析索引按 guest 实例共享，跨 fork/网络 namespace 的 VFS 克隆继续
使用同一索引。索引只持有 socket 弱引用，bind 失败和关闭立即移除表项；使用身份
分配地址作内部键，保留的 Weak 阻止地址复用，地址不向 guest 暴露。pathname
访问仍依赖文件系统及权限；abstract 名称仍按创建 socket 的网络 namespace 隔离。
新索引仅用于控制路径，不进入普通 Unix 字节 I/O，也没有增加全局锁或服务线程。

硬链接/路径替换分别保留原身份；关闭后路径仍存在，connect 返回 ECONNREFUSED，
重新 bind 仍需 unlink。磁盘格式不保存宿主身份，重新打开时 socket inode 为无绑定
状态。Wayland/X11 的陈旧端点清理改为检查同一共享身份。

## 验证

16 项不同的 release Rust 检查通过，包括跨线程读者持有身份时关闭、插入失败
回滚、旧身份/新路径区分、硬链接及权限、跨网络 namespace、overlay/bind/mount
namespace、磁盘恢复、FD 耗尽、SCM_RIGHTS 的递归释放/环拒绝、WAITALL、FIONREAD
及原生 Wayland 协议和陈旧 X11 清理。

ARM64 guest 完成内存/磁盘 × 1/2/4 CPU 的 6 组 Unix endpoint 检查，另有 1/2/4 CPU
的 3 组网络 namespace 检查，共 9 组。相同 endpoint ELF 的修改前 runner 预检查
通过；宿主原生 C endpoint 结果也一致。endpoint 包装器只运行现有 fixture 的
pathname/abstract 部分，未重跑不相关的 parent-death、OOM 或完整图形用例。
network namespace fixture 按现有验收约定使用 `--network full`，没有宿主特权
namespace oracle，也没有据此宣称完整 Docker 或图形兼容性验收。

首轮新增磁盘测试错误地同时打开一个被独占锁定的 store；已改为释放磁盘所有者，
保留旧 socket，再验证恢复后的 inode 无绑定。局部 C 包装器补齐原 fixture 的
`mkdir /tmp` 初始化；network fixture 的初次 loopback 模式也改回既有 full 模式。
只重跑失败/新增项，已经通过且代码不变的检查直接复用，原始失败日志保留在本地。

Release CLI/FFI、三个相关 crate 的严格 release Clippy（含测试）、fmt/diff 检查
通过。CLI/FFI 构建后仅改过测试，生产源码未变，复用该构建。原始材料位于
`target/shared-unix-binding-checks/`；未做整体跑分、IPA、提交或发布。
Linux x86_64 的结果不替代 Apple ARM64/iPad 验收。

## 剩余边界

PathBindings 解析索引、LocalSocket listener/namespace/channel 关系和 SCM_RIGHTS
的 OFD 台账仍属于 owner；bind/connect/listen/close 没有因此全部迁出。共享身份
只拆掉 inode 到这些控制对象的直接引用，不是共享整个 socket、目录、FD 表或
VM 提交，也没有量化整体提速。FIFO、overlay lower、映射页和 PTY 主端目录/信号
寿命仍需继续拆分。
