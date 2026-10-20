# 宿主磁盘读取：异步提交与完成

基线为 `588ad9f`；实现边界见[epoch 服务](../architecture/epoch-services.md)。
本批将普通未映射磁盘文件的大块 read/readv/pread/preadv 拆成提交、后台打开/读取、
协调器提交 offset/atime/inotify/guest 写回三个阶段。最多两个按需创建的持久 I/O 线程，
未完成请求受 guest CPU 数约束。并非所有磁盘操作都已异步化。

后台仅接收独立磁盘对象租约和数字参数；没有 VFS Rc/RefCell、guest Memory、页借用。
共享 OFD 的 offset 操作与同 backing 写入/sync 等待读取完成，独立管道服务可继续。
映射、关闭、exec、取消等 join 前 drain 已提交读取并回复结果，避免重放。对象租约
使已 unlink 文件和存储锁保留到最后一个后台任务释放。无新增 unsafe 或全局 VFS 锁。

## 验证

- Linux x86_64 宿主、4 KiB 页，Core 78、VFS 161、Linux 24 项通过；严格 affected
  Clippy、rustfmt 与生产三终端、共享文件、resize/Ctrl+C/背压/取消通过。
- 原静态 musl 程序八个模式 × 1/2/4 CPU 共 24 组，对照本机 C 输出与 NativeCall
  字节/调用计数。新增 bulk 使用真正磁盘 store；2/4 CPU 分别提交 124/122 次后台读取，
  每次 64 KiB；单 CPU 仍走原路径，提交数为 0。
- 确定性用例保留一个未提交的磁盘完成事件，此时独立 pipe 请求仍能回复；共享 offset
  的读取必须等待。四种读取 API 在 stop 后仍只提交一次，检查完整 32 KiB 内容、offset
  和精确 SVC 指令数。另验证匿名文件/存储锁跨线程寿命、短读与失败不推进 offset。
- Apple ARM64 原生 16 KiB 页验证由本提交的 Actions 执行；不替代 iPad 真机验收。

## 性能对照

同一 release、guest ELF 和输入，测量期间不运行其他构建/测试。bulk 每次固定执行
2048 次 1 MiB native fill，以及 256 次 64 KiB 磁盘读取，四种 API 轮换；两线程、四个
guest CPU。before/after、after/before 各一轮，包含进程启动和临时 store 创建。

| 工作量 | before | after |
| --- | --- | --- |
| 缓存磁盘 bulk，第 1 对 | 0.037658 s | 0.040135 s |
| 缓存磁盘 bulk，第 2 对 | 0.037719 s | 0.041480 s |
| Docker daemon ready | 10.207 s | 10.210 s |
| 缓存镜像 Docker run 到交互 shell | 21.599 s | 23.401 s |
| shell 往返、exit 和 --rm | 2.101 s | 1.802 s |

bulk 两侧 guest 指令数均为 182021，native 调用/字节完全一致；after 每次提交 256 次
后台读取。短缓存读取多了约 2.5–3.8 ms 的排队/完成开销，本组没有加速。这里只含一个
I/O 调用者加一个 native 线程，不能代表多个 I/O 调用者或慢盘时的响应性收益。

Docker 使用与前批相同的缓存 `default-probe` 镜像、1741 inode store、4 CPU/4 GiB，
默认 dockerd 管理的 containerd/runc/overlay2，无拉取、GOMAXPROCS、nofile 或 runtime
特殊参数。单次前后对照不足以归因回退或证明稳定变化；此次没有显示 Docker 提速。
启动期间第二终端采样延迟中位数均约 10.1 ms、p95 均约 30.3 ms，采样精度约 10 ms；
最大值 91.4→40.4 ms 也不足以据此宣称改善。未复现用户 iPad 上的八至十五分钟启动。

这些结果说明后台读取链路可用，尚未证明整体执行效率提升。16 KiB 以下请求仍 inline，
但大块缓存读也存在队列成本；后续需要结合排队压力/真实宿主等待做调度决策。写入的
配额预留、append/短写、同步持久化，MappedData 脏页、单任务读取，以及公平 checkpoint
前 drain 的长等待仍是后续工作。无新 CoreMark、iPad 跑分或 IPA。

原始输出、二进制/源码及证据 SHA-256 见
[manifest](async-disk/manifest.json)。
