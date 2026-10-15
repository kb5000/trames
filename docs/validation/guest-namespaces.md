# Guest namespace 与原生直连网络验证

2026-10-15，开发分支第一批。原有 v0.15.0/build 16 Release 保持不变，本批没有
发布新的 IPA。GPL-3.0-or-later；DNS 适配器复用已有 resolver 源码。

## 已完成的本地验证

- Rust workspace 全 features：301 项测试通过，含 UTS 对象/FD/额度回收、chroot
  路径、DNS UDP/TCP、半关闭、截断、非法查询、队列上限与 loopback 策略。
- `cargo fmt --all --check`、workspace all-targets/all-features Clippy `-D warnings`，
  以及 `aarch64-apple-ios` FFI 编译检查通过。后者不代表 Apple SDK 链接或真机测试。
- AArch64 guest 在 1/4 个 CPU 下验证 fork UTS 共享、clone/unshare 隔离、
  setns 恢复、namespace FD 的 stat/nsfs/ioctl、进程退出后的保活、权限失败、
  chroot 后绝对符号链接/根目录 `..` 与实际 exec。原版 Alpine BusyBox
  unshare/nsenter/chroot 命令通过。
- 默认直连在 1/4 CPU 下验证 DNS UDP/TCP 分帧、peer 身份和半关闭、真实 musl
  getaddrinfo；宿主客户端通过 loopback 和宿主非回环 IPv4 地址访问 guest HTTP，
  IPv6 loopback HTTP 和原生 UDP 入站通过；原版 BusyBox `ip a` 展示实际宿主地址。
- iPad 使用的 C embedding 路径：省略 network 配置时实际启用 Full，真实 musl
  DNS 和宿主 loopback/LAN HTTP 通过；重开 store 时替换旧内置 gateway resolver，
  保留自定义 resolver 内容。初期测试发现的 Full 参数校验和既有文件更新问题已修正。
- 原生网络 36 项 QEMU 对照通过，含内存/磁盘、IPv4/IPv6、短 I/O、SIGPIPE、
  EINTR/restart、UDP/TCP DNS 回退；显式 DNS 文件导入优先于自动默认配置。
- 显式虚拟 packet 网络 27 项 QEMU 对照及真实 Alpine HTTPS apk update/add、
  错误 CA 拒绝通过。旧后端没有被默认选择变更破坏。
- fd exec 的 23 项 Linux 对照在 1/4 CPU 下通过；原有 embedding PTY、磁盘传输、
  取消、暂停/恢复、重开和显式重置回归通过。
- 系统调用审计：318 项中 170 个 implemented_subset、146 个 missing、2 个
  obsolete_stub；全部 148 个无入口编号实测返回 ENOSYS。入口存在不等于完整 ABI。

## 明确未验收

本机宿主的 `ping_group_range` 不允许当前用户创建 ICMP datagram socket，
因此只验证 native raw-echo 适配器和 BusyBox ping/ping6 如实传播宿主权限错误。
不能把这算成实际 echo 成功。CI 中 `TRAMES_REQUIRE_ICMP_EGRESS=1` 强制要求
IPv4/IPv6 原生回包、guest identifier/校验和和 BusyBox ping 全部成功。

当前本地证据不包含 Apple SDK/simulator 运行、iPad Safari/LAN/权限/前后台
行为或网络性能数字。新的 Linux/Apple CI 状态另记在仓库 Issue，不能沿用
v0.15.0 的通过结果。已有 Apple DNS/HTTPS probe 已切换为本批默认网络。

实现边界见[guest 容器设计](../architecture/guest-containers.md)和
[直连网络设计](../architecture/direct-network.md)。mount/PID/net/user namespace、
完整 proc 隔离、pivot_root、cgroup 等不属于本批已完成范围。

## 重现

```sh
cargo test --workspace --all-features --locked
cargo clippy --workspace --all-targets --all-features --locked -- -D warnings
cargo check --target aarch64-apple-ios -p trames-ffi --locked
cargo build --release -p trames-cli -p trames-ffi --locked
bash scripts/build-namespace-fixtures.sh
python3 tests/integration/namespaces.py
python3 tests/integration/direct-network.py
python3 tests/integration/embedding-network.py
python3 tests/integration/fd-exec.py
python3 tests/integration/embedding.py
python3 scripts/audit-syscalls.py
```

依赖已准备的固定 Alpine rootfs、静态 BusyBox、musl 交叉编译器及其他已有 fixture。
单元测试/网络集成需要宿主允许本机 socket；网络策略错误不应通过伪造回复掩盖。
[原始集成日志](guest-namespaces.log)与[源码/输入/日志指纹](guest-namespaces-data.json)
保留本地验证来源。
