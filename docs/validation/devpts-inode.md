# Docker 交互式容器：devpts inode 身份

build30 真机 `docker run -it --rm alpine sh` 被原版 runc 拒绝：
`verify ptmx handle: ptmx handle has wrong inode number: 326417514497`。
原版 runc 1.3.4 的 `libcontainer/console_linux.go:checkPtmxHandle` 要求
真实 devpts magic、inode=2、字符设备5:2。之前devpts将mount/device前缀放入inode，
普通无TTY容器不经过该校验，因此未暴露问题。

修正为Linux devpts局部编号：根1、ptmx2、从端index+3。不同实例仍以st_dev区分，
调度器PTY身份仍guest全局唯一。关闭并重新分配同编号从端可以复用inode；保留的旧FD
继续持有原对象，其link count为0，不能连接到新PTY。bind/O_PATH/实际master打开
使用同一底层设备元数据，不绕过runc校验。

同时修复 `/proc/*/fd` 重新打开设备的语义：O_PATH ptmx重新打开要分配新的
master，而不是复制空PTY字段；从端重新打开需检查lock/master存活并新增长开的从端
引用。脱离挂载后的O_PATH句柄仍指向原devpts实例，不按路径串去寻找其他容器。
只修inode的首轮真实Docker测试越过校验后报ENOTTY，正是这个第二处问题。

Linux原生devpts编号和proc重新打开独立master对照、4项VFS PTY定向测试、
11项proc相关回归（含1项PTY重开，计数有重叠）及严格Clippy通过；见同名目录。
新增 `tests/integration/docker-tty.py`，复制已有原版Docker/Alpine镜像磁盘，验证默认
runc/overlay2的 `docker run -it --rm`，终端stdin/stdout/stderr、输入往返及退出清理。
完整交互链路已通过：4 guest CPU，默认runc/overlay2，stdin/stdout/stderr均为TTY，
输入往返、stderr、正常退出及--rm清理全部成功，退出码0，用时111.671秒。
这是共享x86_64开发机解释ARM64的链路验收用时，不是iPad性能对照。
输入磁盘清单及runner哈希、原始日志见同名目录。镜像来自已有签名Alpine测试盘，
不是一次重新拉取Registry验收。Apple相同4+11项检查通过；[最终build31 IPA和指纹](ipad-1.0-build31.md)已核验。
不重跑未变CPU/MMU/网络/UI/GPU和guest库完整验收。

日志中的pidfd_open/open_tree/fsopen/keyctl以及bpf拒绝也出现在此前已成功的默认
非TTY链路中；本次直接错误是ptmx inode，不把所有ENOSYS同时认定为启动阻塞，
也不把这些能力记为已实现。

边界：本轮--rm断言确认Docker API列表无该容器；日志仍有shim清理时runc exit255警告，
不宣称所有底层runtime状态清理完整。没有在iPad复测或重新验收Registry拉取。
