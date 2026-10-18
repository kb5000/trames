# Docker 隔离基础（1.0.0 后的开发分支）

基线为 `v1.0.0`（`02bbd14871f2ec8d0c4ee824d1a2e67ad2ad2b5c`）。不修改已发布
1.0.0 的 IPA/tag；本次未要求新 IPA。完整版 Docker 的网络仍在
[Issue #29](https://github.com/kb5000/trames/issues/29)跟踪，
[后续数据路径](../architecture/container-network-design.md)明确区分已实现与待实现。

## 实现

- network namespace：私有 TCP/UDP/ICMP 协议栈、down 状态 lo、独立端口与
  抽象 Unix 地址。setns/fork/exec/旧 FD 归属、网络 nsfs 句柄与权限保留。
  文件路径 Unix socket 继续通过共享 inode 跨 netns 访问。netlink/ioctl/proc/sysfs
  不展示不存在的 eth0；proc socket 表按 socket 创建时的 namespace 过滤。
- IPC namespace：独立 mqueue 文件系统身份，已有 mount/FD 不跟随 setns。
  现有每容器 tmpfs/devpts 和 /dev/shm 保留。SysV IPC 与 POSIX mq_* 仍为缺口。
- cgroup namespace：创建时的实际成员组成为视图根；proc 路径及新 cgroup2
  挂载据此显示，不迁移任务，也不取消祖先设备/pids/freezer 限制。
- 网络 sysctl：ping_group_range 和 ip_unprivileged_port_start 按 netns 保存，
  打开的 FD 固定其对象。改变前者实际控制 ICMP datagram 创建，后者作用于
  guest TCP/UDP bind；不能提高 embedding 网络策略。UID/能力、范围、偏移、
  错误与旧 FD 交叉访问有定向检查，未知 sysctl 继续拒绝写入。
- seccomp(277)/PR_SET_SECCOMP/PR_GET_SECCOMP：严格模式及经典 BPF filter，
  64 字节 ARM64 seccomp_data、A/X/scratch、前向跳转验证、不可读 scratch
  拒绝、程序/链预算、叠加优先级、fork/exec 继承与 TSYNC 原子提交。
  ALLOW/ERRNO/TRAP/KILL_THREAD/KILL_PROCESS、无 tracer 的 TRACE 和无 listener
  的 USER_NOTIF 按选定语义处理；未知动作终止，不执行被过滤 syscall。
  notification FD、ptrace、LOG、SPEC_ALLOW 与 TSYNC_ESRCH 等安装选项未实现，
  不返回虚假的成功。状态可在 /proc/PID/status 查看。
- 原版 glibc 测试触发的兼容修复：未设置 SA_RESTORER 时使用 guest VDSO，
  不跳入忽略字段中的垃圾地址；MAP_STACK 按 Linux advisory no-op 接受；
  未启用 GCS 时 CHKFEAT 是保留 X16 的 HINT。

namespace、FD 与 filter 均为协调线程对象，没有新增逐指令/原生访存锁。
网络身份预算同时由 socket 引用保持，关闭最后引用后回收；seccomp 每程序
4096 条，路径预算 32768 条含额外叠加费用。资源控制仍限于已有 pids/freezer/
设备限制，不声称 CPU/memory/io 限额、rootless 或完整 Linux conformance。

## 证据与复现

受影响 net/VFS/Linux/core library、严格 clippy/fmt、本轮 CHKFEAT 单项检查通过。
新的静态 ARM64 C 夹具在实际解释器上验证 1/4 vCPU；seccomp 的 errno、叠加、
fork/exec、TRAP siginfo/可修改返回上下文、TSYNC、KILL_PROCESS 与独立原生
Linux 执行结果一致。网络夹具实际建立私有 TCP/UDP 连接，验证旧 FD、相同
端口、抽象 Unix/路径 Unix 的不同边界、lo、sysctl、不可外连与 setns。
没有将无法取得特权的宿主 netns 作为成功的独立 oracle。

原版签名 Docker 28.3.3-r5、containerd 2.1.5-r2、crun 1.23.1-r0 的诊断已在
4 vCPU 下完成 daemon/import/create/start、默认 IPC/cgroup/seccomp、容器 PID 1、
退出 7、logs 与 remove。容器使用 network=none；daemon 仍为 vfs、bridge=none、
iptables=false，不能把它写成默认 bridge 成功。规范测试另加入 namespace ID、
cgroup 视图和 sysctl 的强制断言，并由 CI 检查。原始二进制未修改。

```sh
bash scripts/build-container-isolation-fixtures.sh
cargo build -p trames-cli --release --locked
python3 tests/integration/container-isolation.py
python3 tests/integration/docker.py --isolated --require-run --cpus 1
```

保留构建脚本需要的 cross GCC/native C 工具链；Docker 脚本需要原版 Alpine
3.22.6 rootfs 下载缓存，APK 仍由 guest apk 验签。CI 仅检查本轮受影响模块，
并编译 ARM64 iOS FFI；不重复 1.0.0 的 UIKit/GPU/GTK/foot/多终端验收，不生成
新 IPA。CLI 阶段的宿主网络结果不是 iPad 真机网络/后台运行证明。
