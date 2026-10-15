# Guest mount namespace 验证

2026-10-15，基于 `1105b1f179421ab103925f50b29d450a61b60b6e` 的第二批容器基础。
本批独立交付 mount namespace；PID namespace 是下一批，不包含在已通过范围内。
没有生成新的 IPA，也没有修改 v0.15.0/build 16 Release。

## 本地已通过

- `cargo test --workspace --all-features --locked`：312 项测试通过。新增 11 项 VFS
  测试覆盖 bind 身份、只读属性、tmpfs 配额/硬链接/改名、namespace FD、rbind、
  pivot_root、mmap 保活、惰性卸载子树、配额失败原子性/回收、chroot 挂载视图、
  挂载字段转义、保留目录能力的 exec 和 noexec/mprotect。
- workspace all-targets/all-features Clippy `-D warnings`、fmt 检查、
  `aarch64-apple-ios` FFI 编译检查通过。iOS 编译检查不等于 SDK 链接或真机验收。
- 新 ARM64 guest fixture 在 1/4 CPU 下验证 clone/unshare/setns mount namespace、
  NEWNS/CLONE_FS 冲突、tmpfs 容量、bind inode/设备身份、只读/noexec、忙卸载、
  detached dirfd、pivot_root、挂载 proc、旧根卸载及父进程不受影响。
- 原版 Alpine BusyBox 的 unshare/nsenter/chroot 回归通过；新增 `unshare -m`、
  `mount -t tmpfs`、bind/remount、`df` 和 `umount` 实际命令通过。
- fd-exec：23 项原生 Linux 对照分别在 1/4 CPU 下通过，包括 memfd、execveat、
  libc fexecve、O_PATH、deleted inode、CLOEXEC 和脚本解释器。
- mmap：内存/磁盘后端的共享与私有映射、fork/COW、EOF/SIGBUS、remap、
  memfd/seals、代码别名失效和调度接口回归通过。
- storage：2 项 QEMU 对照、重开/追加、超过 20 MiB 的流式复制、默认/自动配额、
  宿主路径范围、导出预检、错误清理、独占锁与 SIGKILL 恢复通过。
- 318 项 syscall 审计：173 implemented_subset、143 missing、2 obsolete_stub。
  145 个无入口编号实测均返回 ENOSYS；有入口不代表参数/标志全部实现。

测试过程中修复了默认伪挂载影响 Alpine 导入、BusyBox 的 MS_SILENT/RELATIME、
no-follow exec 的错误优先级、跨 mount 的 FD 路径显示与根外目录能力 exec 等问题。

## 范围与限制

没有新的吞吐量/加速比结论。新增检查在挂载/文件系统系统调用和映射权限变更时
执行，不给普通 CPU 指令或 guest 访存加锁。

这不是完整容器验收：PID/net/user/IPC namespace、shared/slave 传播、overlayfs、
独立 devpts、完整跨进程 proc root 能力、cgroup/seccomp 和 runc/crun 仍待实现。
tmpfs 当前每实例最高 16 MiB/1024 inode，挂载对象每 guest 最多 128 个存活引用；
详细边界见[容器设计](../architecture/guest-containers.md)。

此前基础提交的 Linux/Apple CI 已通过，见[第一批后续结果](guest-namespaces.md)。
本批提交会触发新 CI，状态记录于 [Issue #22](https://github.com/kb5000/trames/issues/22)。
本地结果不能代替本批 Apple SDK/simulator 或物理 iPad 测试。

## 重现与证据

```sh
cargo test --workspace --all-features --locked
cargo clippy --workspace --all-targets --all-features --locked -- -D warnings
cargo fmt --all --check
cargo check -p trames-ffi --target aarch64-apple-ios --locked
cargo build -p trames-cli --locked
cargo build --release -p trames-cli --locked
bash scripts/build-namespace-fixtures.sh
python3 tests/integration/namespaces.py
python3 tests/integration/fd-exec.py
python3 tests/integration/mmap.py
python3 tests/integration/storage.py
python3 scripts/audit-syscalls.py
```

使用已固定的 Alpine rootfs、BusyBox、musl 交叉编译器与现有其他 fixture。
[集成日志](guest-mounts.log)和[源码/输入/日志指纹](guest-mounts-data.json)保留证据。
