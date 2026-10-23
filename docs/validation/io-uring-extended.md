# io_uring 扩展验证

本页保留 28-opcode 阶段记录。后续提供缓冲区和 multishot 见 [新验证](io-uring-multishot.md)。

2026-10-23，在首版 11 个 opcode 上扩到 28 个，补固定缓冲注册、排空、超时和完成
等待。精确范围与约束见 [设计](../architecture/io-uring.md)。没有完整 Linux ABI、
worker 本地 io_uring、iPad 真机或性能提升声明。

## 定向验证

28 项受影响 Rust 测试通过：17 项 ring 引擎、2 项 core enter 生命周期、4 项 pin 内存、
3 项网络 preflight/事务性、原 openat2 与 statx 各 1 项。新增边界包括非法 absolute
open 的 flags 不被错误清除、带 DRAIN 的链接超时、非法 UPDATE 不改旧 deadline、
固定文件取消在原数字 FD 复用后仍匹配旧 OFD、固定缓冲注册失败回滚、EXT_ARG 等待
只导入一次 deadline，以及 pin 的 fork/CLONE_VM、mprotect、unmap、mremap、地址/页
复用、驻留额度、代码版本和 worker 共享读写。此前 4 项未改动的 mmap/FD/失败 exec
检查复用，不重复完整 CPU、Vulkan、Firefox、Docker 或 CoreMark。

真实静态 AArch64 musl ELF 使用标准 Linux UAPI、共享 SQ/CQ/SQE：

- 原 io-uring fixture：1/2/4 CPU × ABI/IPv4 TCP/IPv6 TCP，共 9 组。
- 新 io-uring-extended fixture：同样 9 组。检查文件创建/分配/截断/statx/关闭、
  open_how 约束、跨 enter DRAIN、参数导入、dirfd 关闭复用和 `/proc/self/fd` 路径、
  absolute/count/update/link/ETIME_SUCCESS timer、按 OFD/ALL/ANY 取消、固定缓冲
  跨页/待完成卸载/fork/remap、EXT_ARG 超时/部分提交、Unix socket 全连接生命周期。
  TCP 使用专用 CONNECT/SEND/RECV opcode，独立宿主服务分段回应，检查无重放和 EOF。

内存检查发现并修正两个 pin 边界：保留 arena 引用仍可能在局部重映射时被重用其物理
offset，因此 pin 页先取得独立稳定存储；mremap 可更换 frame index，因此弱 pin 索引
以 SharedPage 身份为键。最后的索引修改只重跑 pin 测试和扩展 ABI 中的 1/2/4 CPU
固定页用例；未受影响的 TCP、原 ABI 和 enter 测试复用已经通过的结果。

Release CLI/FFI 构建、6 个相关 crate 严格 Clippy（含 tests）、cargo fmt、C 的
Wall/Wextra/Werror、Python/shell 语法、git diff 检查均按改动范围执行。当前没有 Apple
构建环境，本轮未打 IPA；尤其宿主 16 KiB MMU 分组仍需后续 Apple 验证。

## 复现与记录

`scripts/build-io-uring-fixture.sh` 编译两个 fixture。沿用原集成脚本：

```sh
python3 tests/integration/io-uring.py --runner target/perf-runtime/release/trames-run \
  --fixture tests/fixtures/bin/io-uring-extended --output target/io-uring-extended-new
```

`--cpus 1 2 4` 为默认配置。仅重测新增 ABI 可直接把 fixture 作为 `/uring-self` 导入，
无附加参数运行。测试未下载软件，不使用外网。原始日志在本地
`target/io-uring-extended-checks/`，统计和源码/产物哈希见
[io-uring-extended-summary.json](io-uring-extended-summary.json)。三个 syscall 的兼容
清单定向更新；旧全 syscall 探测基线和首版报告保留。
