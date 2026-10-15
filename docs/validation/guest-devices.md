# Guest 设备节点与原版容器启动路径

日期：2026-10-15。基线：`abf2d0104ae94984c7f6ec3d050223b05856d915`。
本批修复原版 crun 的 mknodat 阻塞，以及随后暴露的路径、元数据和会话问题。
没有增加系统调用编号，没有修改应用版本，也没有构建新的 IPA。

## 已验证行为

- mknodat 创建字符/块设备 inode，执行 CAP_MKNOD、目录权限、umask 和只读挂载
  检查；设备号、权限、所有者、硬链接和改名结果在磁盘重开后保留。
- null/zero/full/random/urandom 的 I/O 与宿主原生 Linux 对照。修正 `/dev/full`
  零长度 write 同样返回 ENOSPC；空写入不能统一视为成功。
- 未注册设备的 O_PATH/stat 查询和持久化有效，数据打开返回 ENXIO；字符/块类型
  在 getdents 中区分。旧 FD 在 unlink 后继续有效，不产生宿主设备文件。
- guest PTY 设备号复用既有分配器；nodev 阻止新的数据打开但不撤销旧 FD，
  O_PATH 元数据仍可查询。TTY/PTY 不因此获得独立 devpts 实例。
- 磁盘提交失败回滚设备创建；损坏的设备类型、保留字段和 payload 长度在恢复
  清理之前拒绝，保留 manifest 与对象。新设备只占 inode 额度，不占数据文件额度。
  合成的易失设备不能硬链接进持久 inode 表，避免保存超出磁盘分配范围的 inode。
- 1/4 CPU 各运行 30 项原生 Linux 对照：openat2 BENEATH/IN_ROOT、绝对路径及
  绝对/相对链接、dotdot 越界与钳制、NO_SYMLINKS/NO_MAGICLINKS、proc magic
  links、O_PATH|O_NOFOLLOW、非法参数，以及取消链接 FD 的 chmod/chown 身份。
  另在 guest 验证 tmpfs/bind 跨挂载拒绝、离开 bind 的 `..` 和 CACHED EAGAIN。
- metadata 调用刷新 proc 快照，chmod/chown 使用被引用 inode 和挂载；修复
  原版 crun 新建 O_PATH FD 后经 `/proc/self/fd/N` chmod 时取到旧快照的问题。
- mount fixture 增加 `pivot_root(".", ".")`、旧根 FD 切换、递归 private、
  MNT_DETACH 与重复卸载 EINVAL，验证父 namespace 不变、保留旧 FD 仍可使用。
- PID fixture 校正新 namespace init 的 PGID/SID 继承：祖先编号显示 0；首次
  setsid 返回 1，已是会话首领时再次调用返回 EPERM。原版 crun 的 setsid 已通过。
- 320 个 workspace all-feature Rust 测试、strict Clippy、fmt、ARM64 iOS FFI
  编译检查通过。1/4 CPU namespace/cgroup/capability 与原版 BusyBox/capsh、
  sudo 认证/提权/PTY、23 项 fd-exec Linux 对照、runtime-compat、PTY 回归通过。
  持久磁盘测试覆盖 QEMU 对照、重开、超过 20 MiB 导入导出、预算和 SIGKILL 恢复。

设备创建、mount 和 PID namespace 的特权行为没有在本地无特权宿主上运行差分；
本地原生对照只覆盖无需特权的设备 I/O 与 openat2 路径行为。会话继承依据
[Linux v6.12 copy_process](https://github.com/torvalds/linux/blob/v6.12/kernel/fork.c#L2553)，
原版运行时路径来自
[crun 1.23.1 create_dev/do_pivot](https://github.com/containers/crun/blob/d20b23dba05e822b93b82f2f34fd5dada433e0c2/src/libcrun/linux.c#L1688) 和
[safe_openat](https://github.com/containers/crun/blob/d20b23dba05e822b93b82f2f34fd5dada433e0c2/src/libcrun/utils.c#L394)。

## 原版 crun 当前结果

原版签名 Alpine `crun=1.23.1-r0`，4 CPU guest，有效 OCI bundle、静态 BusyBox
`/bin/true`、PID/mount/UTS namespace、private 传播、NNP 和选定能力集合，显式
禁用 cgroup manager。已有设备/路径/根切换/会话错误消失，现在输出：

```text
Running container on PID: `4`
cannot detect HOME environment variable, setting default
Writing container status
signalfd: Function not implemented
```

退出状态仍为 1，**没有通过完整容器启动/运行验收**。signalfd4（74）是新的实际
阻塞；open_tree/keyctl 的 ENOSYS 仍是运行时可处理的探测。下一批需要把 signal FD
接入已有待决信号、阻塞读取和 poll/epoll，再验证原版运行时的生命周期。

设备与 openat2 fixture 已加入 Linux CI；crun 脚本继续输出 BLOCKED，保留每步
退出状态、原始日志和包 SHA-256，不充当成功测试。

## 范围与复现

没有块设备驱动、宿主设备透传、socket inode mknod、独立 devpts 或完整 Linux
设备模型；cgroup 仍为无资源控制器的成员子集。CACHED 始终返回 EAGAIN；
user/IPC/net namespace、seccomp、传播挂载等仍待实现。
设备标签扩展当前 v3 manifest，旧二进制不认识新标签时会拒绝打开该磁盘。
没有新增 CPU/guest memory 热路径锁，也没有性能跑分或新加速比主张。

```sh
cargo build --release -p trames-cli
bash scripts/build-device-fixtures.sh
python3 tests/integration/devices.py
bash scripts/build-namespace-fixtures.sh
python3 tests/integration/namespaces.py
python3 tests/integration/container-probe.py
python3 scripts/audit-syscalls.py
```

系统调用清单保持 176 个 implemented subset、140 个 missing、2 个 obsolete；
142 个未分派编号均返回 ENOSYS。CPU 指令清单不变。

[原始日志](guest-devices.log) · [源码/二进制指纹](guest-devices-data.json) ·
[crun 输出与包指纹](guest-devices-crun.json) · [实现范围](../architecture/guest-containers.md)
