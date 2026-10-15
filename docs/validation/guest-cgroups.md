# Guest cgroup v2 域成员与 dumpable

日期：2026-10-15。基线：`e869969c1a3dd0959a1856276739b0267323b4b9`。
修复原版 crun 在 `/sys/fs/cgroup` 上的 ENOENT，并补它随后需要的 dumpable。
不更改应用版本号；没有构建或签名本批 IPA。

## 实现与验证

- 独立 cgroup2 挂载：statfs、mountinfo、组目录、procs/threads、type、events、
  stat 和 proc cgroup 视图。迁移整个线程组、fork 继承、线程退出、exit_group、
  非主线程 exec 和 PID namespace init 退出清理均使用实际任务成员。
- 1/4 CPU guest 测试覆盖上述生命周期、namespace-local PID 读写、新挂载共享
  同一层级、mount namespace 只读隔离、非法控制写入及无权限拒绝。
- 删除后重建同路径，旧控制 FD 与旧 bind mount 返回 ENODEV；不会操作替代组。
  Rust 边界测试覆盖 128 个 live/dying 组上限、最后引用关闭后回收、32 层深度
  及失败不留下节点。组不消耗根磁盘额度，重新打开同一磁盘时会话状态清空。
- PR_GET/SET_DUMPABLE 支持 0/1，验证 fork 复制、CLONE_VM 共享、普通 exec
  重置、特权 exec 与 UID 变化清零。exec 在保留旧 VFS 的同时提交新 dumpable。
- 无特权宿主 Linux 与 guest 对照 proc maps/environ/auxv/ns/exe 访问，保留
  stat/lstat/O_PATH 元数据查询；使用 NNP 使普通 exec 不恢复 guest root 能力。
  另在 guest 内验证 root exec 恢复能力时保持不可转储，不把它当作普通 exec。
- 317 个 workspace all-feature Rust 测试、strict Clippy、fmt、ARM64 iOS FFI
  编译检查通过；既有 namespace、capability/capsh、原版 sudo、23 项 fd-exec
  原生 Linux 对照、runtime-compat 和 PTY 回归通过。
- 系统调用编号数不变：176 个 implemented subset、140 个 missing、2 个 obsolete；
  全部 142 个未分派编号重新探测为 ENOSYS。CPU 实现未改变。

组管理/迁移要求 CAP_SYS_ADMIN，不支持委派、threaded 模式、cgroup namespace、
CPU/memory/pids 控制器、freeze/kill 或事件通知。controllers 为空，资源限制请求
明确拒绝。dumpable 仅接入选定 proc 访问语义，没有 core 文件写出和完整 ptrace。
没有新增 CPU/guest memory 热路径锁，也没有新的性能跑分或加速比主张。

## 原版 crun 的下一处阻塞

签名 Alpine `crun=1.23.1-r0`、上游 commit
`d20b23dba05e822b93b82f2f34fd5dada433e0c2`，使用有效 OCI bundle、静态 BusyBox
`/bin/true`、PID/mount/UTS namespace、private 传播配置、NNP、选定 capability，
显式禁用 cgroup manager。rootfs 包含标准 dev/proc/sys/tmp 目录。

旧的 cgroup statfs 错误和 PR_SET_DUMPABLE 错误消失，现在输出：

```text
Unsharing namespace: `pid`
Unsharing namespace: `mount`
Unsharing namespace: `uts`
Running container on PID: `4`
mknodat `/dev/null`: Not supported
```

最终退出状态 1，尚未执行容器里的 `/bin/true`。open_tree 和 keyctl ENOSYS
触发 crun 自身的可选功能回退，不是最终退出原因。下一批需要补标准字符设备
节点创建、持久化和权限，再继续原版启动流程；不能把本次进展视为完整容器验收。

诊断脚本记录 BLOCKED，不加入 CI 冒充成功测试；cgroup/dumpable 测试已加入 CI。

```sh
cargo build --release -p trames-cli
bash scripts/build-cgroup-fixtures.sh
python3 tests/integration/cgroups.py
python3 tests/integration/container-probe.py
python3 scripts/audit-syscalls.py
```

[测试日志](guest-cgroups.log) · [源码/二进制指纹](guest-cgroups-data.json) ·
[原版 crun 输出与包指纹](guest-cgroups-crun.json) ·
[实现范围](../architecture/guest-containers.md)
