# Guest 内容器：namespace 基础

目标是在同一个 Trames guest 内运行原版 Linux 容器工具。容器共享解释器、
调度器和 guest 内核对象；当前不做宿主多实例管理器或自己的 OCI 启动器。
后续用未经修改的 guest `runc` / `crun` 验证 Linux ABI，再考虑 Docker/Podman。

## 已实现

| 接口 | 当前范围 |
| --- | --- |
| `unshare` | `CLONE_NEWUTS`、`CLONE_NEWNS`、`CLONE_FS`、`CLONE_FILES`；未知组合与额度失败不改变调用者状态；NEWNS 隐含独立 fs context |
| `clone` | UTS/mount namespace 共享或复制；NEWNS 与 CLONE_FS 同时使用返回 EINVAL |
| `setns` | UTS/mount namespace FD，类型 0 或对应 CLONE 标志；mount setns 拒绝仍共享 fs context 的任务 |
| `uname`、`sethostname`、`setdomainname` | 调用者 UTS，名称最长 64 字节；当前按 guest euid 0 授权 |
| namespace FD | `/proc/PID/ns/{uts,mnt}`、task/TID 形式、dup、O_PATH、stat、nsfs statfs、`NS_GET_NSTYPE`；FD 保持 namespace 存活 |
| `chroot` | 独立 `(mount,inode)` fs.root，绝对路径/绝对符号链接从新根解析，根内 `..` 不能向上；保留 cwd/dirfd |
| `mount` | tmpfs、proc、sysfs、共享 devpts、bind/rbind、选定 remount；所有挂载初始为 private，支持 make-private/recursive-private |
| `umount2` | 普通卸载检查子挂载和 FD/cwd/root/exec/mmap 引用；MNT_DETACH 断开整个子树，已有能力仍能访问；UMOUNT_NOFOLLOW |
| `pivot_root` | 挂载根和 put_old 目录/祖先关系检查，旧根接到新根下，同 namespace 内位于旧根的 fs.root/cwd 一并切换 |
| 挂载视图 | `/proc/PID/{mounts,mountinfo}` 来自实际挂载树，按该进程 chroot 过滤/显示路径并转义字段；statfs/statx MNT_ID/fdinfo 使用实际挂载身份 |

## 挂载身份与文件内容

路径解析、cwd、root、打开的文件、执行中的 ELF 和文件映射保存挂载引用。
同一个 inode 经不同 bind mount 可具有不同的挂载 ID 和权限。bind 不复制文件；
底层修改通过两条路径可见，跨挂载 rename/link 返回 EXDEV。
`..` 在挂载根跨回挂载点的父目录，绝对符号链接从进程根重新解析。

mount namespace 复制挂载对象，保留底层文件系统/inode；不会复制根磁盘内容。
既有 FD 保持原挂载能力，不因 unshare 被改写。惰性卸载保存仍有能力引用的
子树，最后引用释放后回收额度。proc 快照持有弱引用，避免历史视图保活。

`MS_RDONLY` 约束路径修改、FD 写入/截断和共享可写映射；`MS_NOSUID` 影响
exec 凭据；`MS_NODEV` 拒绝设备打开；`MS_NOEXEC` 拒绝 exec、可执行 mmap，
并保留映射的最大执行权限供后续 mprotect 检查。已有可写映射使只读重挂载
返回 EBUSY。这是选定模型，不声称实现 Linux superblock 的全部 remount 语义。
`MS_RELATIME` 在普通文件 read/pread 时按 mtime/ctime 或 24 小时条件更新 atime；
`MS_SILENT` 和旧版 mount magic 被兼容。不支持的挂载行为明确返回错误。

这些对象只在现有内核调度边界访问，没有给普通 CPU 指令/访存增加锁或权限检查。
挂载点使用缓存路径验证，祖先改名后才重新查找 inode，避免每次 lookup 扫描磁盘树。

## 配额与尚缺的部分

- 每个 guest 会话最多 128 个存活挂载对象，含 namespace 副本和被 FD/映射保留的
  挂载；耗尽 ENOSPC，释放后可再创建。UTS 谱系也有 128 个存活对象额度。
- tmpfs 独立于磁盘 manifest，默认 16 MiB / 1024 inode；`size`、`nr_inodes` 可降低
  上限，`mode` 设置根目录权限。卸载/会话结束后内容不持久化。这里的内存上限
  与根磁盘默认 16 GiB 预算无关；尚未接入统一的 tmpfs/guest RAM 回收机制。
- 所有挂载均为 private；shared/slave/unbindable 传播、MS_MOVE、overlayfs、块设备
  文件系统及 open_tree/move_mount/fsopen 等新挂载 API 尚缺。
- proc/sysfs 保持当前只读端点；devpts 挂载共享现有 PTY 池，明确不接受
  `newinstance`，不能宣称容器已有独立终端或设备隔离。
- PID namespace 与 proc 挂载所绑定的 PID 视图还未实现。跨进程 `/proc/PID/root`
  等完整 magic-link 能力、user/capability、IPC/net namespace、cgroup、seccomp
  仍缺。现在不能运行一般意义上的完整容器，也不构成完整隔离边界。

`chroot` 不自动 chdir/关闭既有目录描述符；根外 cwd/dirfd 继续是可用能力。
程序应自行 `chdir("/")`。本批没有开放可写 sysctl 或模拟成功的安全控制接口。

## 网络选择

根网络使用[原生直连](direct-network.md)。每个 guest 会话拥有自己的 socket
和资源对象，普通 TCP/UDP 使用宿主网络，不经过 10.0.0.1 网关。这不产生独占
宿主 IP/端口空间；多个会话仍会竞争同一个宿主监听端口。

未来 `CLONE_NEWNET` 创建 guest 内独立网络对象，已有 socket 保持创建时的
namespace 归属，setns 只影响后续创建。私有 loopback、端口空间、veth/bridge
和可选发布端口在这一层实现，根网络无需再套 NAT。现有显式
`--virtual-network` packet fabric 可作为基础，但不是 `CLONE_NEWNET`。

## 后续顺序

1. PID namespace：父子层级、各层 PID 映射、PID 1 退出/收养语义、wait/kill、
   proc 挂载绑定的 PID 视图；不会只把 getpid 改为 1。
2. User/capability、IPC/net namespace、进程控制和 cgroup 必需子集；用真实
   容器工具探测结果排序，未知限制明确报错。
3. 原版 guest runc/crun 的最小 rootfs、多个隔离进程和生命周期测试；之后
   扩展完整容器引擎、overlayfs、cgroup 控制器及更广的安全接口。

## 验证

```sh
bash scripts/build-namespace-fixtures.sh
python3 tests/integration/namespaces.py
python3 tests/integration/direct-network.py
```

namespace 集成测试覆盖 1/4 CPU 下 UTS、chroot 和 mount ABI，再运行原版
Alpine BusyBox 的 unshare/nsenter/chroot/mount/umount/df。
首批证据见[UTS/直连网络验证](../validation/guest-namespaces.md)，本批证据见
[挂载隔离验证](../validation/guest-mounts.md)。
