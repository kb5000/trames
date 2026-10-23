# io_uring 提供缓冲区与 multishot 验证

2026-10-23，扩到 30 个 opcode。新增 PROVIDE_BUFFERS/REMOVE_BUFFERS、READ/RECV
缓冲选择、用户描述符 buffer ring 注册/卸载/status、ACCEPT/RECV/POLL/TIMEOUT
multishot、poll 更新。完整范围与未支持部分见 [设计](../architecture/io-uring.md)。
执行仍在原 joined 所有者；没有新增后台线程、worker-local ring、Apple 构建或总体
提速声明。

## 验证范围

26 项 ring 单测及 1 项 pin 元数据单测通过，新增覆盖 CQ/元数据额度、保留终结空间、
溢出 flags、eventfd 数量、64 次预算轮转/饥饿、跨 VM 拒绝取缓冲、坏 tail/u16 回绕、
注册验证、重复 timer 更新、普通 timer 延后 issue、背压后取消过期期限，以及栈上
跨页描述符读取。此前 core enter/普通 pin/网络 preflight/openat2/statx 的未改测试
直接复用。timer 的收尾更改只重跑受影响 timer 检查，未重复全部已过项目。

真实静态 ARM64 musl ELF，使用标准 Linux 头和映射队列，在 1/2/4 CPU 下：

- 新 fixture：ABI、IPv4/IPv6 分段接收、IPv4/IPv6 满 CQ 空闲等待，共 15 组。
- 原 fixture 与前一轮扩展 fixture：各只跑受 CQ/timer 改动影响的 ABI，共 6 组；
  原 host TCP 场景不重复。总计 21 个 guest case。

ABI 用例检查经典池选择/移除/错误 ID、stream 多次接收与 EOF、耗尽后重提、MORE
不提前释放链接、取消/线程退出的终结 CQE、poll 去重/更新/remove、accept 安装真实
FD 与 CLOEXEC/NONBLOCK、buffer ring 发布/补充/status/卸载/映射身份、datagram 截断/
零长度消息、有限次数 timer，以及 CQ 满后不再消耗缓冲/恢复时无丢失或重放。

## 空闲等待检查

满 CQ 的请求还持有 ready 的原生 TCP 时，guest 留着 CQ 不消费并 sleep 150 ms，
随后逐项取回 128 字节并取消。测试同时测子进程宿主 CPU 时间，要求小于 wall 的
75%，用来抓“宿主 poll 总是立即返回”的空转，不作为吞吐跑分。
本轮六组 IPv4/IPv6 × 1/2/4 CPU 实测整个进程为 153..156 ms wall、约 4..7 ms CPU。
常规 CoreMark、CPU 全指令、Vulkan、Firefox、Docker 均未重复运行。

修正包括：CQ/请求额度耗尽后，不把对应 multishot socket 或过期 timer 放进等待
集合；未实际 issue 的相对/count timer 不提前计时；已报告的 multishot edge poll
不持续用 level-ready FD 唤醒 owner。仍使用原调度检查点观察 epoch，检测延迟受原
调度周期影响。

## 构建与复现

Release CLI/FFI、6 个相关 crate 严格 Clippy（含 tests）、格式、C 严格警告、Python/
shell 语法与 diff 检查通过。本轮没有 IPA/iPad 真机验证。

```sh
bash scripts/build-io-uring-fixture.sh
python3 tests/integration/io-uring.py --runner target/perf-runtime/release/trames-run \
  --fixture tests/fixtures/bin/io-uring-multishot --pressure --output target/io-uring-multishot-new
```

`--abi-only` 可仅运行 fixture 的 ABI，`--cpus` 可限制配置。`--pressure` 仅用于新
multishot fixture。原始日志在本地 `target/io-uring-multishot-checks/`，汇总和产物/
源码哈希见 [io-uring-multishot-summary.json](io-uring-multishot-summary.json)。兼容性
清单只定向更新 425/426/427，没有伪称重跑旧的全 syscall 探测。
