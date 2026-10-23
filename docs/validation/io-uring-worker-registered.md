# io_uring 固定资源、链接与排空的 worker 验证

2026-10-23，Linux 云环境，Release / cached interpreter / 无 JIT。
[机器记录](io-uring-worker-registered-summary.json)保存源文件、产物及证据哈希。
兼容实现仍为 30 个 opcode，本轮 worker 子集扩到 10 个；尚未进行 Apple/iPad 验证。
这是第十六批的历史记录；后续提供缓冲、计时/取消及 CQ 等待见
[第十七批验证](io-uring-worker-async.md)。

## 本轮变化

固定文件、READ_FIXED/WRITE_FIXED、IO_LINK/HARDLINK、IO_DRAIN 与 ASYNC hint 接入原
worker ring。资源、guest 内存访问及等待仍在执行 guest 的 worker 或原事件驱动运行队列
上完成；不新增服务线程、每请求协调 RPC、消息轮询或全局数据锁。

固定页/驻留额度以 Arc 共享原 SharedPage/Charge；不复制整个 VM，不给 Rc/RefCell 或
Memory 增加 unsafe Send。pin 仍遵循原来的一次稳定存储提升协议，跨 worker/epoch
共享同一字节。WRITE_FIXED 复用 worker 暂存区；已接受请求保留 pin/OFD，注销、关闭
及数字 FD 复用不改绑。固定普通文件复用原 FileEpoch 的权限、限制、位置和字节实现。
重复 ring FD 在同一任务内共享固定能力表；不同任务仍分别捕获权限与限制。

队列锁只仲裁游标、接受额度、依赖和 CQ 发布，字节/文件/socket 操作在锁外。DRAIN
索引包含其他 worker 已领取的在途项；测试暂停一个实际数据访问时，同 ring 无关 NOP
仍可完成，排空及后继必须等待它终结。普通链接按未截断的请求长度识别短 I/O 并取消
后继，硬链接继续；iovec 元数据只导入一次。未来的依赖项不提前订阅 socket/timer。

检查共享 pin 时发现并修复了旧快照的私有免锁标记：稳定页注销后重新注册可能没有
改变存储/权限，旧标记却仍表示唯一所有者。新测试在修复前失败。现在注册只使涉及页
的所有权快照失效，pin 页使用原区域 guard；相邻未注册页保留免锁快速路径。独占与
只读多 View 准入都已检查，普通 CPU 访存未增加查表或屏障。

## 定向验证

44 项不同 Rust 检查通过：21 项 worker ring、7 项内存 pin/私有所有权、7 项 VFS 文件
能力、2 项 Linux 文件 ABI 和 7 项 owner ring 边界。覆盖固定页跨 VM/解除映射/保护/
注销/epoch 交接、额度最后所有者释放、原 OFD/只读权限/位置、稀疏及坏 slot、短向量
链接、硬链接错误、EAGAIN、并发在途 DRAIN 和重复 ring FD 能力共享。

最后局部调整后跑了其中 18 项；另外 26 项复用本轮已通过且路径未受影响的结果。
不同用例按名称去重，重复执行未增加数量。逐项证据和复用范围见机器记录。

25 组不同 ARM64 guest 检查通过：新固定资源 fixture 的 1/2/4 CPU ABI、IPv4/IPv6 TCP
共 9 组；原 worker ABI 3 组、worker TCP 4 组、原始/扩展/multishot ABI 各 3 组。
最终 runner 跑了固定资源 9 组与受 pin 注册影响的 multishot 3 组；其余 13 组复用
本轮前面结果，各自保留 runner/fixture 哈希。未重复完整 CPU/GPU/Firefox/Docker/
CoreMark 或全 syscall probe。

新 ABI 每组 128 批、共 422 个 SQE，使用跨页固定缓冲、原数字 FD 已关闭并复用的
固定 pipe/普通文件、链接/硬链接及每批末尾 DRAIN，检查替代 FD 没有被误写。
TCP 同样关闭原数字 FD，服务端延迟 30 ms/20 ms 分段响应后半关闭，检查跨 cohort
等待、EOF 和解除注册后的 FIN；不只验证一次立即完成的发送。

| 用例 | worker io_uring enter | worker I/O park | 协调服务请求 |
| --- | ---: | ---: | ---: |
| abi-1 | 0 | 0 | 0 |
| tcp4-1 | 0 | 0 | 0 |
| tcp6-1 | 0 | 0 | 0 |
| abi-2 | 128 | 107 | 0 |
| tcp4-2 | 3 | 11 | 0 |
| tcp6-2 | 3 | 12 | 0 |
| abi-4 | 128 | 97 | 0 |
| tcp4-4 | 3 | 12 | 0 |
| tcp6-4 | 3 | 12 | 0 |

2/4 CPU 的 128 批固定资源请求全部在 worker 完成；TCP 的发送、延迟读取和 EOF
共 3 次 enter 也全部留在 worker。park 次数随调度变化，不能当作吞吐指标。
`service_requests=0` 表示样例未请求协调服务，不包含 setup/register 和正常 cohort
准备/回收边界，也不能推断 CoreMark 或整个产品的提速比例。

## 构建与剩余范围

Release CLI/FFI、六 crate all-target 严格 Clippy、格式、C 警告、Python/shell 语法及
diff 检查通过。原始证据留在 `target/io-uring-owned-checks/`。

setup/register、提供缓冲、multishot、io_uring timer/取消、EXT_ARG、FD/VM 生命周期
及原受限文件服务仍沿原入口。单任务串行路径、cohort 控制和映射/进程检查点仍存在；
本轮没有构建 IPA。后续继续把提供缓冲、multishot、计时/取消接到同一完成/等待基础。
总体进度见[协调器拆分](../architecture/coordinator-removal.md)。
