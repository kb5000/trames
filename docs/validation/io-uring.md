# io_uring 基础兼容验证

本页保留 2026-10-22 首版记录。当前扩展范围与新测试见 [扩展验证](io-uring-extended.md)。

2026-10-22，在当前协调器拆分开发代码上新增三个 syscall 入口和 11 个 opcode。
范围、资源/并发边界与未支持模式见 [设计](../architecture/io-uring.md)。

## 结果

13 项新 Rust 单测和 4 项受影响的原测试通过。新项检查非法 setup/索引、SQ/CQ 回绕、
CQ 满后的无损保留/背压、短读导致链接取消、GETEVENTS 不重复提交、跨任务/VM 缓冲
隔离、固定文件注册回滚、私有 FD 表异常恢复、关闭 ring 后弱缓存释放、1 MiB 向量
元数据限额和部分提交、FSYNC 与未知 ENTER flags 在消费 SQ 前失败、提交者关闭 ring
FD 后继续推进、阻塞 enter 在 FD 关闭/复用后仍完成原请求。原测试覆盖
普通文件映射别名/COW/截断/关闭、只读映射、CLONE_FILES 变更身份和失败 exec 保留状态。

实际静态 ARM64 musl ELF 的 9 项集成通过：1/2/4 CPU 各运行一次 ABI/生命周期用例，
再分别连接 IPv4/IPv6 独立宿主 loopback TCP 服务。测试使用真实 mmap 的标准 params、
SQE、SQ array、CQE，不用测试专用 syscall。覆盖 NOP/探测/普通与硬链接、scalar/vector
读写、off=-1/定位/共享文件位置、无效缓冲、固定文件更新和关闭后 FD 复用、异步管道、
poll/取消、ring epoll、eventfd、timeout/remove、CQ 溢出、fork 地址空间隔离、线程
退出和 exec 取消。TCP 服务延迟并分段返回 pong，检查 ping 只收到一次、完整输出和 EOF。

Release CLI/FFI 构建、5 个相关 crate 严格 Clippy（含 tests）、格式、C 严格警告、Python/
shell 语法与差分检查通过。收尾补齐阻塞 enter 和活跃请求的 ring 生命周期后，只重跑
受影响的 ring 单测与 9 项 guest；原文件映射/FD 表/失败 exec 的 4 项通过结果复用。
没有重复完整 CPU/网络/图形/浏览器/Docker 验收，也未重跑 CoreMark。

本轮环境为 Linux，未构建 IPA 或执行 Apple ARM64/iPad 真机测试。没有整体提速数据；
首版 ring 请求仍由原 joined 所有者推进，没有把这些系统调用计为新增 worker 本地路径。

## 复现与记录

`scripts/build-io-uring-fixture.sh` 使用原 musl 交叉编译器及 Linux UAPI 头编译 fixture。
`tests/integration/io-uring.py --runner … --output …` 执行 1/2/4 CPU 用例；可用 `--cpus`
只运行受影响配置。fixture 通过 `--exec-file` 作为实际 guest 文件导入，以便测试
`/proc/self/exe` 和 exec 生命周期。独立宿主服务仅使用 loopback，不下载软件或访问外网。

原始日志保留在本地 `target/io-uring-checks/`，必要统计/源码和产物哈希见
[io-uring-summary.json](io-uring-summary.json)。兼容性清单仅定向更新 425/426/427；
原始全 syscall 探测基线保留，没有把历史结果当成本轮全量重测。
