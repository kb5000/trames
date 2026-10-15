# Guest 内容器：第一批基础接口

目标是在同一个 Trames guest 内运行 Linux 容器工具。容器继续共享解释器、
调度器和 guest 内核对象；当前不做宿主多实例管理器或自己的 OCI 启动器。
后续用未经修改的 guest `runc` / `crun` 验证 Linux ABI，再考虑 Docker/Podman。

## 已实现

| 接口 | 本批范围 |
| --- | --- |
| `unshare` | `CLONE_NEWUTS`、`CLONE_FS`、`CLONE_FILES`；不认识的组合在修改状态前返回 EINVAL |
| `clone` | 新增 `CLONE_NEWUTS`；普通 fork/clone 继续共享原 UTS 对象 |
| `setns` | UTS namespace FD，类型 0 或 `CLONE_NEWUTS`；类型不匹配返回 EINVAL |
| `uname`、`sethostname`、`setdomainname` | 读取/修改调用者的 UTS；名称最长 64 字节；当前按 guest euid 0 授权 |
| namespace FD | `/proc/PID/ns/uts`、`/proc/PID/task/TID/ns/uts`、符号链接别名、dup、O_PATH、stat、nsfs statfs、`NS_GET_NSTYPE` |
| `chroot` | 独立 fs.root，绝对路径与绝对符号链接从新根解析，根内 `..` 不能向上；继承、CLONE_FS 共享与 unshare 分离 |

UTS 对象带稳定 inode 身份，namespace FD 保持对象存活，原进程退出后仍可
`setns` 进入。proc 快照仅持有弱引用，避免历史进程信息无限保留 namespace。
每个初始 UTS 谱系最多 128 个存活对象，耗尽返回 ENOSPC；最后引用释放后归还额度。
这些对象只在现有内核调度边界访问，不为 CPU 指令或普通 guest 访存添加锁。

`chroot` 遵循 Linux 的 cwd/dirfd 保留规则：它不会自动 chdir 或关闭已有目录
描述符。程序需要自己 `chdir("/")`；保留在根外的 cwd/dirfd 仍是可用能力。
它不是完整容器隔离。`/proc/PID/root` 等视图尚未按隔离挂载树重构，不能据此
声称 proc、文件系统或特权容器边界已完整。

真实 Alpine BusyBox 的 `unshare -u`、`nsenter -u/proc/self/ns/uts` 和
静态 shell 的 `chroot`/exec 已纳入集成测试。`hostname` 通过系统调用修改名称；
本批没有开放 `/proc/sys/kernel/hostname` 的 sysctl 写接口。

## 网络选择

根网络采用[原生直连](direct-network.md)。每个 guest 会话持有自己的 socket
和资源对象，普通 TCP/UDP 直接使用宿主网络，不经过 10.0.0.1 网关。
这不产生独占的宿主 IP 或端口空间；多个会话仍会竞争同一个宿主监听端口。

未来 `CLONE_NEWNET` 应创建 guest 内的独立网络对象，已有 socket 保持创建时的
namespace 归属，`setns` 只影响后续 socket 创建。容器的私有 loopback、端口空间、
veth/bridge 和可选发布端口在这一层实现；根网络不必为了容器再套一层 NAT。
已有显式 `--virtual-network` packet fabric 可作为实现基础，本批没有把它假装成
`CLONE_NEWNET`。guest 容器的 host-network 模式将使用所在 guest 的根网络。

## 后续顺序

1. Mount namespace：独立挂载表、mount/umount2、bind、传播属性、pivot_root、
   挂载身份与 proc mountinfo；路径查找从 `(mount, inode)` 开始。
2. PID namespace：父子层级、各层 PID 映射、PID 1 退出/收养语义、wait/kill、
   proc 挂载所绑定的 PID 视图。不能只把 getpid 改成 1。
3. User/capability 与权限、IPC namespace、net namespace、进程控制和 cgroup
   必需子集；用真实容器工具的探测结果排序，未知限制明确报错。
4. 原版 guest runc/crun 的最小 rootfs、多个隔离进程和生命周期测试；之后才
   扩展完整容器引擎、overlayfs、cgroup 控制器和更广的安全接口。

目前 `CLONE_NEWNS/NEWPID/NEWNET/NEWUSER/NEWIPC`、mount/pivot_root、clone3、
uid/gid 映射、cgroup 和 seccomp 仍未实现，不能运行一般意义上的完整容器。

## 验证

```sh
bash scripts/build-namespace-fixtures.sh
python3 tests/integration/namespaces.py
python3 tests/integration/direct-network.py
```

第一项在 1/4 个 guest CPU 下验证 UTS 共享/隔离、clone/unshare/setns、FD 生命周期、
stat/ioctl、权限以及 chroot 路径/exec，再执行原版 BusyBox 命令。网络验证范围和
平台限制见[本批验证记录](../validation/guest-namespaces.md)。
