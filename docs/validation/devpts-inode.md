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

Linux原生devpts编号对照、3项VFS PTY定向测试和严格Clippy通过；见同名目录。
新增 `tests/integration/docker-tty.py`，复制已有原版Docker/Alpine镜像磁盘，验证默认
runc/overlay2的 `docker run -it --rm`，终端stdin/stdout/stderr、输入往返及退出清理。
该完整交互链路正在执行；尚不记为通过。Apple定向验证与build31 IPA随后记录。
不重跑未变CPU/MMU/网络/UI/GPU和guest库完整验收。

日志中的pidfd_open/open_tree/fsopen/keyctl以及bpf拒绝也出现在此前已成功的默认
非TTY链路中；本次直接错误是ptmx inode，不把所有ENOSYS同时认定为启动阻塞，
也不把这些能力记为已实现。
