# 共享映射状态，移除重复 inode 元数据工作

2026-10-23，第 34 批。机器记录见
[shared-mapped-state-summary.json](shared-mapped-state-summary.json)。

## 改动及边界

MappedData/FileMapping 共用 Arc<MappingState>，其中稳定身份和原子变更世代
不再依赖 Node。普通文件和 memfd 保留实际 PlainFile/SharedFile 的长度能力，
每次 EOF 检查读取实时长度，不缓存 mmap 时的长度。文件增长、截断以及 EOF
所在的部分页语义不变；长度能力可跨线程持有，不反向保活 inode。原子世代仅用于
缓存失效，采用 relaxed；字节发布和映射布局继续由原有所有权/访存协议约束。

overlay Lower 仍需跟随原 inode；volatile 伪节点可在刷新时整体替换内容。这两类
保留动态 owner 检查和变更域查找，避免旧存储能力错误放行已截断/替换的映射。

原实现每次映射写入都借用 inode、调用宿主时钟并更新 mtime。现在在页面首次
变脏时更新 mtime，类似 mmap 首次写入触发的脏页记账；成功回写/截断清理 dirty
后，下一次写入重新更新时间。仍然**每次写入**都更新 page version、映射变更域
和全局 backing token，代码别名、独占监视器及增量 worker snapshot 不会遗漏写入。
CPU 写入、native span 和 worker 写入后的提交共用这一逻辑。

没有增加普通匿名访存检查、布局锁或服务线程。FilePage 仍使用原 RefCell 存储；
[此前退化的 Arc/RwLock 原型](shared-file-page-experiment.md)继续保持撤回。
首次脏写的 mtime、完整页布局/回写、Node/Lower、FD/OFD、VM/task 所有权仍待迁移。
本批没有新增 worker syscall，也没有撤掉映射提交 join。

## 验证

- 24 项不同的 release Rust 检查通过。新增跨线程实时长度/域读取、inode 寿命、
  持有 inode 独占借用期间的重复 CPU/native 脏写、每次失效、sync 后重新记时、
  Lower 截断以及 volatile 内容替换检查。其余覆盖 COW、live EOF、映射别名、
  code token、memfd、overlay、挂载写引用与磁盘恢复。
- 8 组 ARM64 guest 检查通过：内存/磁盘分别使用 1/2/4 CPU，以及跨进程的
  磁盘写入/重新打开验证。mmap fixture 未变，复用第 31 批相同源码/产物 hash 的
  3 项原生映射及持久化结果；没有重跑无关套件。
- Release CLI/FFI、严格 Clippy、格式和差异空白检查通过。

先完成 23 项 Rust 检查、guest 验证和性能测量，随后审查补上 volatile 节点的
owner fallback。只追加/重跑这项与相关 Lower、域身份、脏写路径共 4 项检查，
复用其余通过结果。最终生产产物重新构建；测量时的 runner 另存并记录 hash。
该补充不改变普通文件/memfd 的读写主体。没有制作 IPA，也未进行 Apple/iPad 验收。

## 定向性能

使用 [固定工作量](../../tests/fixtures/mapped-access-bench.c) 和
[交替比较脚本](../../tests/performance/mapped-access.py)：同一个静态 ARM64 ELF，
5,000,000 轮一页范围内的 volatile 64-bit 写/读，最后验证校验和及真实 backing。
普通文件和 memfd 各做 3 对 before/after，固定同一宿主 CPU，使用 release runner、
一个 guest CPU；12 次 guest 和 2 次原生结果检查全部通过。

| 映射类型 | 第 33 批 CPU 时间中位数 | 本轮 CPU 时间中位数 | 耗时变化 |
| --- | ---: | ---: | ---: |
| 普通文件 MAP_SHARED | 0.59204 s | 0.47192 s | −20.29% |
| memfd MAP_SHARED | 0.58852 s | 0.47711 s | −18.93% |

这是 Linux x86_64 宿主上执行 ARM64 guest 的映射热路径诊断，不是 CoreMark、
Docker 或 iPad 整体性能结论。未据此为整个产品宣称加速比例。原始日志和测量
runner 保留于本地 `target/shared-mapped-state-checks/`。
