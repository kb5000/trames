# 共享 VM 分配上下文与 worker brk 查询

2026-10-23，第 24 批。删除最后一处原 ResourceChanges 的状态传播。

## 变更与边界

- brk base/current 与 mmap 搜索游标按实际 Memory identity 共享；独立 fork 复制。
  Files/FS 的共享选择不决定 VM 域，成功 exec 创建新上下文，失败 exec 保留旧域。
  删除 sync_heap_context、share_heap_from 以及每次 syscall 的 heap 快照/比较。
- 只读 ProgramBreak 能力为共享 AtomicU64，brk(0) 在当前 worker 一次 relaxed load
  返回，先经过 seccomp。真实映射提交仍由原 VM 所有者完成，成功后发布新 break；
  该原子值不是页表同步工具，不增加普通 guest 访存屏障。
- mmap 游标仍是 owner 域内的 Cell，与 Memory 的映射/权限/配额事务一起修改。
  共享分配上下文不意味着映射已经能并行提交；增长/缩小 brk、mmap/mremap、
  VM/task 生命周期及 FD/OFD/路径操作的剩余协调边界保持明确。

原 Files、FS、sighand、heap 的逐任务资源传播至此都已删除。cohort 准备、整组停止、
受限文件服务和生命周期协调仍存在，完整内核迁移尚未完成。

## 验证

21 项受影响 Release Rust 检查通过：Linux 7、core 14。覆盖共享与私有 VM、Files/FS
独立性、mmap 游标/提示/虚拟保留区、mremap 强制搬移、配额失败回滚、proc 映射、
fork/exec 和 worker seccomp。最终仅重跑修改了读取次数的两个 brk 检查，其余复用。

宿主 Linux 原生及 ARM64 guest 的内存/磁盘后端各通过 1/2/4 CPU：CLONE_VM 与
CLONE_FILES/CLONE_FS 的组合、真实 brk 增长、成功 exec 分离及失败 exec 保留。
fixture 使用独立 mmap clone 栈，恢复原 break 后再进行 libc/pthread 分配，避免
测试自身破坏宿主 malloc 状态。Release CLI/FFI、四个受影响 crate all-targets 严格
Clippy、格式和差异检查通过。没有重复完整 CPU、网络、图形或应用验收。

## 定向对照

同一静态 ARM64 ELF，两线程共 512 次原始 brk(0) 查询；完整域用例与 benchmark
模式分开，每个 CPU 配置交错 3 次。

| Guest CPU | 改前中位数 | 改后中位数 | 并行批次（完整基准程序） |
| --- | ---: | ---: | --- |
| 1 | 0.329 ms | 0.318 ms | 0 → 0 |
| 2 | 11.029 ms | 0.171 ms | 513–516 → 3–4 |
| 4 | 10.955 ms | 0.172 ms | 514–516 → 3–4 |

service_requests 两版都为 0；减少的是整组返回与重建。worker_syscalls 包含
yield/clock 等调用，不作为精确 brk 次数。数据来自 Linux x86_64 宿主的很短查询
密集负载，不能推断 iPad、CoreMark 或整个应用的加速比。

证据：[shared-vm-allocation-summary.json](shared-vm-allocation-summary.json)。原始产物
在 `target/shared-vm-allocation-checks/`。没有提交、发布或构建 IPA。
