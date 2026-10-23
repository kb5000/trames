# OFD 叶对象：OOM 状态与 BPF 程序共享寿命

2026-10-23，第 35 批。机器记录见
[shared-ofd-leaves-summary.json](shared-ofd-leaves-summary.json)。

## 所有权变化

OomTask 的任务身份由 Arc 持有，打开的 proc 文件和运行信息仅保存 Weak。
线程共享的 OOM 偏好使用另一个 Arc<AtomicU32>：score 与特权设置的 minimum
都在 −1000…1000 内，按两个有符号 16-bit 值存入一个原子。

特权写入一次 store 同时设置分数/下限；非特权写入用 CAS 在提交时重新核对下限，
不会把并发特权更新的新 floor 覆盖成旧值。fork 从同一次 load 复制完整状态，
CLONE_THREAD 共享偏好，任务身份仍各自独立。全部使用 relaxed；此元数据不发布
guest 字节，不需要新增任务锁或内存屏障。最后任务引用可在 worker 释放，proc
弱引用随即失效，同组存活线程仍可使用偏好。没有增加 OOM 杀进程策略。

BPF 已验证、不可变的指令对象由 Rc 改为 Arc。匿名 FD、cgroup attachment 和
执行引用共用同一程序，注册表改为 std::sync::Weak；不复制指令、不加执行锁。
执行引用可以在 worker 保留身份并读取代码，最后释放后旧 ID 不再可重新打开，
下一次 load 的原有弱引用清理立即回收配额。注册/附加算法及验证器保持原实现。

这是完整 OFD 迁移的两项依赖拆分。proc 权限与 task 列表、BPF 注册表/附加集合、
FD 安装和完整 OFD 仍在原 owner 路径；没有新增 worker syscall 准入，没有删除
这些操作的 join，也没有给 CPU/native 普通访存增加代码。

## 验证

- 12 项定向 release Rust 检查通过，包括四线程并发分数/权限下限更新及 fork
  一致快照、worker 最后任务释放、线程/进程生命周期、proc DAC/只读挂载与 ESRCH、
  BPF verifier、设备权限、附加/替换/继承、worker 执行引用及配额回收。
- OOM 和 cgroup BPF 原有 ARM64 guest fixture 各在内存/磁盘 × 1/2/4 CPU 通过，
  共 12 组。改动前 runner 各跑 1 组作为行为对照，输出一致。
- Release CLI/FFI、严格 release Clippy、格式和差异空白检查通过。

本机原生 OOM 对照在打开 `/proc/self/oom_score_adj` 时返回 EROFS，因为执行环境的
proc 为只读；尚未执行到待测行为，记录为环境受限，**不计作原生通过**。没有改变
宿主挂载或权限，也没有在宿主 cgroup 安装 BPF。guest 验证保留原有断言及改动前
对照，原始输出见本地 `target/shared-ofd-leaves-checks/`。

构建时修正了 Arc import 和当前 nightly 的 `try_update` 名称；第一次测试脚本
因构建尚未完成被完成状态检查拦下，等待完成后才执行上述 12 项测试。没有使用
旧测试产物或忽略失败的行为测试。上一批 mmap 验证/性能结果按源码未变复用，
未重跑 CPU、CoreMark、Docker、浏览器或图形套件；本批不宣称定量性能收益。

未制作 IPA，也未进行 Apple/iPad 验收。完整映射页仍需处理 EOF 占位页增长后的
真实共享存储接入及 VM 访问租约；不会用提前分配稀疏页或逐次布局锁绕过这个边界。
