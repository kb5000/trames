# Guest inode 通知验证

2026-10-16：新增 AArch64 `inotify_init1`（26）、`inotify_add_watch`（27）、
`inotify_rm_watch`（28）。这是 guest VFS 的真实事件队列，不调用宿主 inotify。
普通持久化文件和 tmpfs 使用同一套 inode、dentry 和打开描述语义。

支持创建、删除、重命名的配对 cookie、目录 ISDIR、文件访问/修改、
chmod/chown/utimens/xattr 元数据、打开和最后一次关闭通知。目录移动后，
打开文件仍关联原 dentry；unlink 后的打开描述继续通知原父目录，
`IN_EXCL_UNLINK` 可过滤这些通知。文件最后一个引用消失时发出
DELETE_SELF/IGNORED；目录删除立即移除自身 watch。

watch 按 inode 识别，支持 ONLYDIR、DONT_FOLLOW、MASK_ADD、MASK_CREATE、
ONESHOT、EXCL_UNLINK；遵守路径搜索和读取权限。实例支持 NONBLOCK/CLOEXEC、
dup/fork 共享、完整记录读取、小缓冲 EINVAL、空队列 EAGAIN、FIONREAD，
poll 及 epoll level/edge/oneshot。最多 8,192 个 watch/实例，16,384 个普通
排队记录；相邻重复事件合并，溢出产生 wd=-1 的 IN_Q_OVERFLOW。

事件由已有 VFS 协调线程提交，无额外全局锁。FD 的读写通过保留的 dentry
找到父目录，不扫描整个文件系统，也不在通知热路径创建临时数组。
这些索引是瞬态状态，不修改磁盘 manifest 格式；磁盘重开时重建父目录关系。

## 验证

```sh
bash scripts/build-inotify-fixtures.sh
python3 tests/integration/inotify.py
cargo test -p trames-vfs --lib
```

同一份 C fixture 分别在原生 Linux、ARM64 QEMU、Trames 内存 VFS 和磁盘 VFS
通过。覆盖 create/open/write/pwrite/chmod/truncate、rename cookie、pread、
最后一个 dup 关闭、删除、自身 watch 移除、目录标记、mask 更新、oneshot、
短缓冲和 EAGAIN、fork 共享、阻塞 read，以及 epoll ET 排空后再次唤醒。
还对照匿名 memfd 原始 FD 的 FMODE_NONOTIFY、proc FD 重开、EOF 不通知，
以及最后关闭时的 DELETE_SELF/IGNORED；inotify 的 proc 路径会先刷新进程视图。

Rust 测试另外覆盖 overflow、未 unlink 通知与 EXCL_UNLINK 的区别、CLOEXEC、
磁盘重开后的父目录识别，以及失败的持久化事务不发出创建通知。
VFS 共 110 项测试在 x86_64 开发宿主和 ARM64/QEMU 上通过。
工作区 364 项测试、严格 Clippy、iOS ARM64 core/distro/FFI 交叉检查通过。

`tests/fixtures/node-inotify.cjs` 在原版 ARM64/musl Node 24.18.1 上输出
`node-inotify-ok`，验证实际 libuv `fs.watch` 能收到 rename/change；此探测也已
加入原有 Node 22 CI 流程。没有替换宿主 Node 来运行 guest 程序。

## 边界

- 目录树的递归监听由应用逐目录注册，符合 Linux inotify 的使用方式。
- 尚未生成 IN_UNMOUNT；动态 proc/sys/cgroup 端点变化、宿主 Bridge 外部写入
  没有完整通知。FIFO/设备的数据访问通知尚未补全。mmap 直接写入不产生
  MODIFY，和 Linux 的 inotify 范围一致。
- 无路径的 inode 元数据操作向现有父目录链接广播；硬链接别名的元数据事件
  扇出尚未逐项复现 Linux 的 dentry 选择。
- 没有 fanotify、权限拦截或宿主文件监视；watch/队列预算尚非 Linux 的每 UID
  sysctl 计费模型。错误返回和已实现功能不等于完整 Linux ABI。
