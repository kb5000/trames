# 共享 FilePage 原型：性能回退，未采用

2026-10-23，在第 33 批之后尝试共享映射页本体，结果未进入生产代码，也不计入
已完成迁移批次。机器记录见
[shared-file-page-experiment-summary.json](shared-file-page-experiment-summary.json)。

## 尝试与正确性

原型将 FilePage 由 Rc 改为 Arc，存储布局 RefCell 改为 RwLock，dirty/version
改为 relaxed 原子。已发布的 SharedPage 仍由 worker 直接访问；经过 FilePage
的共享页写入仅保留布局读锁，真正字节访问使用现有 64-byte 区域协议，因此不同
区域写者不会互相排斥。回写在释放字节守卫之前清理 dirty，避免覆盖后来的脏写标记。

22 项 release Rust 检查通过，包括共享页不同区域同时写、并发写入/回写、最后
页引用在 worker 释放物理预算、映射别名、COW、EOF、代码 token、memfd/native
视图和 overlay。8 组 ARM64 guest 映射/持久化检查及 Release CLI/FFI、严格
Clippy 通过。原生 mmap fixture 的已有结果按源码/产物 hash 复用。

## 拒绝原因

专门检查串行 mmap 的 CPU 访存：同一个静态 ARM64 ELF，5,000,000 轮 volatile
64-bit 写入再读取，工作集一页，最后检查校验和及实际 backing。普通文件与 memfd
各做 3 对 before/after，交替顺序，固定一个可用宿主 CPU。每次都通过原生 C 的
相同结果断言。使用 release runner；这不是 CoreMark 分数，也不代表 iPad 整体速度。

| 映射类型 | 修改前 CPU 时间中位数 | 原型 CPU 时间中位数 | 时间变化 |
| --- | ---: | ---: | ---: |
| 普通文件 MAP_SHARED | 0.58246 s | 0.74071 s | +27.17% |
| memfd MAP_SHARED | 0.59145 s | 0.74427 s | +25.84% |

原因在于串行 FileMapping 读写仍逐次进入 FilePage，布局锁虽然没有加到 worker
View 的普通访存，却增加了串行映射热路径成本。功能通过不足以接受这个回退，
因此恢复第 33 批的映射实现；其他已验证的 Unix/FIFO 拆分和硬链接修复全部保留。
恢复后重新构建，并核对生产源文件及 CLI/FFI 的 hash 与第 33 批一致；不重跑源码
未变的已通过套件。初次基准碰到 runner 默认 10M 指令限制，正式比较改为 200M，
所有记录中的正式运行均正常结束。

原型源文件、候选 runner、日志和原始结果留在本地
`target/shared-file-page-checks/`。保留可复用的
[工作量](../../tests/fixtures/mapped-access-bench.c) 和
[比较脚本](../../tests/performance/mapped-access.py)；归档测量使用本地同等循环
包装器，具体源文件/ELF/runner hash 在机器记录中。没有制作 IPA 或发布。

## 下一步约束

先解决串行映射的稳定页访问能力缓存与失效：必须继续检查 live EOF/COW/权限，
截断、映射增长、memfd 物理页替换时不能读取旧指针；执行缓存和独占监视器也必须
保留原失效语义。CPU/native 统一复用已有 SharedPage/MMU 能力，避免新增逐次
布局锁，再共享页对象和 inode。

overlay 下层引用也不能简单换成 PlainFile 快照：下层后续 mmap 写入、截断和
存储形态变化仍需通过稳定文件身份可见；已打开下层 inode 的删除/通知寿命不能
被提前结束。文件数据、路径元数据和实际 inode 生命周期应一起拆分，而不是给
完整 VFS 增加一把全局锁。

## Stable byte capabilities, 2026-10-25

The follow-up shares the actual FilePage allocation with Arc, without placing its
layout RwLock on every CPU/native access. Each owner-local FileMapping retains a
byte capability and its layout generation. Publication or physical replacement
invalidates that generation; the next access refreshes once under the layout
lock. Cached capabilities retain their physical allocation, and live EOF,
permissions, COW and provider byte generations retain their existing checks.

Before publication, the provider alone accesses its allocation through scoped
unpublished operations on the existing SharedPage byte cells. These unsafe
operations require exclusive byte ownership and no SharedPage monitor observer;
the provider maintains its original version/dirty hooks. The first publication
changes the layout tag before exposing an Arc to a worker and invalidates owner
caches. All subsequent shared CPU/native accesses use the original region
protocol. No VFS/Memory/Rc graph gains an unsafe Send implementation, and ordinary
accesses gain no permission policy, global lock or memory barrier.

The owner FileMapping and MappedData APIs remain coordinator-local. Layout
replacement/publication still requires the existing joined boundary. Arc storage
and cached capabilities are prerequisites for inode/OFD sharing, not permission
to call those owner APIs concurrently or to remove the VM/backing synchronization
boundary. In particular, a future mapping commit must quiesce genuinely related
actors before replacing an unpublished capability.

Native Apple ARM64 validation passes 281 VFS, 172 Core, 121 Linux and six region
unit tests. One VFS, three Core and one Linux manual test remain ignored. Five
VFS loopback cases denied by the filesystem sandbox passed separately with local
network access. Scoped strict Clippy passes. The added checks exercise worker
last-release, warmed capability promotion, truncate/re-extension with stale cache
references, live EOF and the transition to guarded bytes without a publication
copy. Six original guest mmap/persistence/scheduler cases pass against both the
unchanged baseline runner and candidate, using the same locally cross-built ELF.
These local guest comparisons do not replace the independent QEMU acceptance.

The original five-million-iteration mapped-access workload was run in alternating
baseline/candidate order three times for ordinary files and memfd. Median CPU time
ratios were 1.014 and 1.023 respectively. This small pilot has no confidence
interval or host CPU pinning and does not prove a speedup or exact non-regression;
it did not reproduce the rejected 26–27% per-access layout-lock regression.
Machine-readable results and source/ELF/runner hashes are in
[the capability summary](shared-file-page-capabilities-summary.json).
No iPad, Docker or Firefox performance claim is made. Complete FD/OFD ownership,
VM-local commits and persistent contexts remain release gates in
[#36](https://github.com/kb5000/trames/issues/36).
