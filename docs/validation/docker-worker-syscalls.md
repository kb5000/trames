# Docker 启动、第二终端响应与 worker 系统调用

2026-10-20，用户在 1.2.0 build 33 上执行已缓存镜像的
`docker run -it --rm alpine sh`，从连接 shim 到 ARP 警告约 8 分钟；
CPU 显示约一核忙，第二终端也卡。本轮仍未在用户 iPad 复现这段八分钟等待。
开发机使用真正生产多终端 Session FFI、4 个默认 guest CPU、4 GiB 总/单进程预算、
Wayland、原版默认 dockerd/containerd/runc/overlay2 与同一缓存导入镜像 `default-probe`。
没有下载镜像、缩小 nofile、限制 Go 核数或更换 runtime；测量未与构建/测试并行。
该 rootfs 有 1741 个 inode/路径，不是用户完整 rootfs 或其 `alpine:latest` digest。
Linux x86_64 实际页为 4 KiB，不作为 Apple 16 KiB 或 iPad 性能结果。

## 归因与改动

启动阶段采样 93% 左右落在 `trames-local` 协调线程。基线约 58.6% 顶层 PC
在 FFI 以外，不能全部归因于内核；补采绝对 PC 确认主要在 libc，包含大量短字节比较
和分配/释放。FFI 内 `Node::path_in` 约 8.8%、同类路径树遍历约 8.1%，
FD 表克隆/销毁与 `fd_snapshot` 也可见。这是顶层 PC 和线程 CPU 采样，非完整调用栈。

已改动：

- proc/sysfs inode 实际存于 PseudoFs，原先会先扫描无关磁盘路径表，失败后才查 PseudoFs。
  现在先处理这些端点；稳定 proc/sysfs mount root 直接使用原 key。
  cgroup/devpts 仍保留自己的存储查找；bind、namespace、权限检查不放宽。
- CLONE_FILES 同步只在描述符表发生变化时复制。所有可变 map 访问统一失效版本标记，
  fork 仍各自持有描述符引用，open description/offset/status flag 继续共享；
  CLOEXEC、close/exec、分叉后独立修改和退出的关闭/EOF 责任保留。没有增加算法锁。
- `getpid/getppid/getuid/geteuid/getgid/getegid/gettid`、`getcpu`、
  `clock_gettime/clock_getres` 共十种调用，可以在 CPU worker 内返回并继续执行，
  不发送 syscall 完成消息，不要求其他 CPU 返回协调器。
  身份/可见 PID/CPU slot 在 epoch 开始捕获为普通值，时钟实时读取；
  guest 写入直接使用解释器的 `CpuMemory` 并行视图和同样的检查/并发模型。
  时钟共用协调器/worker 实现，避免两套 ABI 逻辑。
- seccomp 或 syscall trace 开启时仍完整走协调器；TSYNC 安装、进程/权限/映射修改
  发生在 join 后，不会被旧快照跳过。全局预算、SVC 退休、exclusive 清除和错误码保留。
  `--metrics` 新增 `worker_syscalls`，已包含在总 `syscalls` 中。

## 同输入测量

每个变体一次，顺序旧→仅 VFS→VFS 加并行 syscall。三次早期探索记录另行标明；
有效旧版额外测量为 43.129s、41.569s，不合并成统计置信区间。
基线优化 FFI 的 Core/CPU/Memory/Linux/VFS/Net 与主分支 d1d250d 源码一致；
其无关 native GPU 后端早于 1.2 的 private image 修复，本次未开启 GPU accelerator。
库 SHA、输入 manifest SHA、原始结果/终端输出和线程时间线均留存。

| 阶段 | 旧路径 | 仅 VFS | VFS + worker syscall |
| --- | ---: | ---: | ---: |
| daemon 到 API ready | 13.887s | 12.454s | 10.157s |
| run 到真实内层 shell 提示符 | 44.294s | 30.854s | 21.702s |
| TTY 往返/exit 到 run 返回 | 2.601s | 2.310s | 1.784s |
| 第二终端响应 p50 | 20.2ms | 10.1ms | 10.1ms |
| 第二终端响应 p95 | 50.4ms | 80.8ms | 20.4ms |
| 第二终端最慢响应 | 172.1ms | 233.1ms | 70.7ms |
| run 阶段协调线程 CPU 时间 | 41.71s | 28.15s | 19.58s |

组合启动比 2.041×，耗时减少 51.0%。VFS 中间版的响应尾部反而较差，未声称每项
优化都改善延迟；最终组合的单次结果改善，尚无重复次数支持可靠 p95 或最坏时延保证。
响应探针在真正的第二 Alpine PTY 中每 200ms 发送一个 printf，最多一个请求未完成，
10ms 轮询输出，响应按完成时的阶段分类；不是 UIKit 输入延迟或硬件周期。
CPU 时间来自 200ms 采样的 `/proc/self/task`，阶段边界内首末样本，略短于完整墙钟。
仍主要使用一个协调核，第一批并行不等于整个 Linux 内核已并行化。

入口：

```sh
cc -O2 -shared -fPIC -Wall -Wextra -Werror tests/performance/session-sampler.c \
  -ldl -o /tmp/session-sampler.so
python3 tests/performance/session-docker-tty.py \
  --library target/release/libtrames_ffi.so --store target/docker-default/store \
  --output target/docker-parallel-probe --debug-daemon --peer-probe \
  --sampler /tmp/session-sampler.so
```

每次复制同一冻结 store。`--peer-probe`/`--sampler` 仅供 Linux 诊断，正常应用不开启。
原始 host maps/ASLR 地址只留本地，仓库保留聚合归因、线程计时与 guest 日志。
[数据与范围](docker-worker-syscalls/results.json)。

## 验证与后续边界

Linux Core 60 项、VFS 155 项通过；既有 Wayland 4 项和 VFS 1 项 ignore 保留。
失败的网络沙箱项目仅重跑失败项，未重复已通过项目。严格相关 Clippy 与格式检查通过。
生产三终端 PTY、共享进程/文件、独立 cwd/env、resize、Ctrl+C、背压、关闭/重开、
快速取消通过。原版静态 musl pthread/atomics/futex/fork/exec/affinity/getcpu fixture
与既有已验证基线单核输出一致，新版 1/2/4 核均通过，峰值分别 0/2/4；
本环境没有新的 QEMU oracle，也未用该通用 fixture 的零 worker query 数宣称快路径覆盖。
专门 worker 测试检查连续 32770 次查询无需协调器、精确预算、写内存权限/跨页/错误码，
以及 seccomp 过滤调用不能绕过协调器。Apple 原生页粒度的对应检查由 CI 执行。

剩余目标是继续扩大真实并行范围：

1. 可独立执行的文件/网络 I/O 从协调器拆成提交/完成；使用有生命周期的 handle，
   不携带 Rc/RefCell 或长期借用 guest bytes，等待 I/O 时允许其他 CPU/终端推进。
2. VFS 路径、mount namespace、共享 FD 表分别划分同步范围，采用结构快照或细粒度
   读写同步；避免把整个 syscall 放进一把全局锁。proc 元数据仍需进一步减少重复构造。
3. 映射修改只暂停实际共享该 VM/受影响 backing 的 worker；fork/exec/exit_group、
   seccomp TSYNC、跨进程信号保留所需的定向 rendezvous。
4. futex compare/enqueue/wake 按等待地址协调，保持 ARM 原子性和内存次序，
   不依赖目前的全核停顿来保证语义。

这轮没有把 Rc/RefCell 标成 Send，也没有让无保护的 VFS 可变结构直接跨线程。
ARP 的 AF_PACKET 与 cgroup `memory.events` 警告仍是兼容性缺项，保留在 #29；
没有为消除日志假装实现完整内存控制器。用户 iPad 八分钟等待/第二终端严重卡顿和
新版真机耗时仍需验证，Issue 保持开放。
