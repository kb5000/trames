# 普通文件的 epoch 内服务

2026-10-20，基线 `ec1c0f41af05b93816eca367a048143128ee874b`。
新增普通文件 read/write、readv/writev、pread/pwrite、preadv/pwritev、fsync/fdatasync
服务；pipe/embedding stdio 增加 readv/writev。只挂起请求者，其他 CPU/native worker
继续执行。VFS 请求仍由一个协调器依次处理，不是宿主磁盘 I/O 已完全并行化。
准入机制见[架构说明](../architecture/epoch-services.md)，[证据清单](epoch-files/manifest.json)
记录源码、二进制、fixture 与原始数据摘要。本轮没有 IPA 或 iPad/CoreMark 新成绩。

## 一致性与 ABI

准入检查 OpenDescription 实际保留的 inode，不按路径名猜测。文件只要在任意 VM 中
仍有 FileMapping，就转回原来的 join/commit；包括只读/private 映射。硬链接、rename/
unlink 与同路径替换不会绕过检查。overlay 旧只读描述符的下层映射仍阻止其服务，
copy-up 后的新上层独立判断。没有调用者没有映射、其他 VM 有映射时的绕过。

SharedFile 可能由宿主显示/GPU 消费，继续保守 join。动态 proc/sysfs/cgroup、设备、
socket/PTY 等保持原边界；有限 RLIMIT_FSIZE 文件写入保留原 SIGXFSZ 路径。
streams/files 候选位图在同一次 FD 扫描中生成，按表版本缓存并一起失效；它不是
权限/映射缓存，每次服务仍重查保留对象。mmap 不修改 FD 表也不会导致越过准入。

Linux joined 与 service 路径使用同一套 scalar/vector/positioned helper 和 VFS，
保留地址预检、权限、offset/append、配额、通知及持久化处理。使用调用者的并行 View，
没有从协调器用串行 Memory 访问运行中的 VM，也没有把 Rc/RefCell 标为 Send。

新增单元验证覆盖八种标量/向量/定位 I/O、fsync/fdatasync、positioned offset 保持、
向量第二段 EFAULT 不消费输入、未提交 MAP_SHARED 写入先 join 再读取最新数据、
有限文件大小限制的 EFBIG/SIGXFSZ、跨 VM/硬链接/rename/unlink/路径替换、overlay 下层
与 copy-up 独立性、共享宿主 backing 拒绝，以及 FD 候选缓存失效。

本地 Linux23、VFS158 通过；Core 初次 69 项通过，新增映射测试的地址准备出现 Overlap，
修正后八项 service 测试通过，共 71 项独立 Core 测试，未变的通过结果复用。
最终共享宿主候选排除重跑两项相关 VFS 测试；五个受影响 crate 的严格 Clippy 通过。
原版静态 musl fixture 在 metadata/pipe/file/vector 四种模式下各运行 1/2/4 CPU，
12 种组合与本机 C 语义输出一致，原生字节计数准确，无 fault/reject/fallback。
生产三终端的独立 PTY、共享文件、私有 cwd/env、resize、Ctrl+C、背压及关闭/重开/取消通过。
本机 C oracle 不替代整个 guest CPU 的差分验收。Apple ARM64/16 KiB 页结果以关联
Actions 为准；未改动的 CPU/MMU/GPU/UI 检查复用，不重复跑分。
Apple 本机 C 输出 oracle 使用 mutex/cond barrier，并将可选 fdatasync 映射为 fsync；
guest 原版 musl 程序仍实际执行 Linux fdatasync，未用此 host 适配代替 guest 验收。

## 固定混合工作量

Linux x86_64、4 KiB 宿主页、release，4 guest CPU、两个 pthread。
每组 4096 次 1 MiB 原生 memset（4 GiB），另一线程做 32768 轮普通文件操作，
标量或向量模式均包含顺序/定位读写、lseek 和周期性 fsync/fdatasync、mmap、dup/close。
前后同一个 guest ELF/输入，每次 **229598 个 syscall**，所有数据检查与原生字节计数一致。

| 模式 | 基线两次 | 候选两次 |
| --- | ---: | ---: |
| 文件标量 + 原生填充 | 0.656 / 1.536 s | 0.434 / 0.430 s |
| 文件向量 + 原生填充 | 1.405 / 1.554 s | 0.441 / 0.434 s |

每种模式执行 before/after、after/before 两组，构建和其他测试均在计时前完成。
基线波动明显，样本短且各两次，不给出稳定的应用加速比例或 iPad 预测。
这是减少 peer syscall 反复打断 native worker 的混合负载；原生 memset 算法没改变。
[完整结果与计数](epoch-files/mixed/results.json)保留每次 stdout/stderr。

## 默认 Docker 生产会话

与上一轮相同冻结 1741-inode store、已缓存 `default-probe` 测试镜像、默认
dockerd/托管 containerd/runc/overlay2、4 CPU/4 GiB、交互 TTY，同时轮询第二终端。
不包含下载，也不是用户完整 rootfs 或确切 `alpine:latest` digest。

| 一次配对 | daemon ready | run 到真实 shell 提示符 | exit/--rm |
| --- | ---: | ---: | ---: |
| 基线 | 11.277 s | 22.385 s | 1.893 s |
| 候选 | 10.115 s | 20.702 s | 1.904 s |

本组 run 时间低约 7.5%，但仅一对，且与上一轮 20–23 秒范围重叠，**不足以证明稳定的
Docker 提速**。保留全部原始结果，不重复计时来挑选更好的数字；iPad 十几分钟问题
仍待实测。本轮没有放回 Go/nofile/runtime 特殊配置。

映射文件一致性、真正的宿主 I/O 提交/完成、socket/PTY、动态 proc、futex/映射/进程
操作及 stateful Vulkan 仍需继续拆分同步范围。此轮没有为这些路径宣称完全并行化。
