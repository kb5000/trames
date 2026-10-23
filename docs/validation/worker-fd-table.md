# 描述符元数据域与 worker 控制

2026-10-23，第 19 批。结果见 [JSON](worker-fd-table-summary.json)。这次共享的是 FD 的
描述符元数据；完整 FD map/OFD、inode、网络和 VM/task 状态尚未全部迁出协调器。
完整后续路线和完成判据见 [共享内核状态](../architecture/shared-kernel-state.md)。

## 最终实现

fd_table.rs 明确区分结构身份与描述符元数据域；原 OFD 继续共享。CLOEXEC 成为每个
描述符独立的 Arc<AtomicBool>，F_SETFD 与 close_range(flags=4) 在 worker 直接更新，
owner 与 worker 使用同一设置/range 算法。查询读取活标志，修改不触发结构 cache
失效、服务消息或全体 checkpoint。原子的 relaxed 顺序不发布资源字节/权限/表成员。

- fork 复制描述符标志、共享 OFD；dup 产生独立标志，SCM_RIGHTS 接收也创建自己的标志。
- CLONE_FILES 显式绑定同一个元数据域，不依赖下一次 owner map 同步才能观察更改。
- unshare(CLONE_FILES)、close_range(UNSHARE) 脱离元数据；只 unshare FS 保持 Files。
- 成功 exec 先脱离再 close-exec，剩余描述符的标志也不再影响旧共享表；失败 exec 不
  提前脱离。原有关闭、进程锁、信号和线程组清理继续走原生命周期主体。
- 结构身份/文件字节能力仍可跨 fork 复用。新的标志缓存绑定新的元数据域，已有直接
  字节缓存不会因为复制 CLOEXEC 再构建。既有 FileEpoch 共享能力断言验证了此点。

创建/关闭/dup、range flags=0/2/6 和 exec/unshare 本身仍可 checkpoint。没有把设置
CLOEXEC 当作关闭成功，没有把完整 Rc/RefCell 的 VFS/Memory 声明为 Send。
当前每个描述符多一个小原子 cell；fork 需要独立 cell。CLONE_FILES 的构造仍先克隆
Linux/Vfs 再显式绑定，会有短暂的元数据准备分配；完整共享 Files/OFD 后应直接共享。
这里没有声称 fork/线程创建零分配，也没有增加普通 CPU/MMU 访存工作。

## 受影响验证

123 项不同的 Rust 检查通过：VFS 108、core 15，其中 5 项新增。范围包括描述符域、
FD 复用、dup/fork/CLOEXEC、记录锁/IPC/PTY/映射/网络资源，以及实际 clone 的四种
VM/FILES 组合、FS-only unshare、UNSHARE range、失败 exec、seccomp ALLOW/拒绝。
原有 CLONE_VM 预算/访存不复制、能力 cache 复用、共享资源同步检查也通过。

真实 C fixture 在宿主原生运行两种模式，ARM64 guest 在 1/2/4 CPU 分别运行：

- 控制模式：两个 guest 线程握手，每轮 7 次标志操作、64 轮。观察线程验证共享标志，
  主线程检查 dup 独立性、range 标记和清除。双核/四核各精确完成 448 次 worker 操作。
- 生命周期模式：clone 的 VM/FILES 四组合、CLOEXEC|UNSHARE、共享表成功 exec 与失败
  exec。exec 后修改未关闭 FD 的标志不影响旧表；失败后修改仍共享。三个 CPU 配置的
  输出均与原生一致。这些生命周期模式不要求 service/checkpoint 为零。

另外，exec 提交路径发生改动，因此编译并运行原有 fd-exec/fd-exec-child：23 项
原生场景在 1/4 CPU 的 46 个结果均一致，包括 execveat/fexecve、O_PATH、删除文件、
proc magic link、权限、CLOEXEC ELF/shebang 和重执行。没有重跑完整兼容性探针。

Release CLI/FFI 与五 crate 的 Release all-targets 严格 Clippy 通过；fmt、Python AST、
源/runner SHA 和差异空白检查通过。没有打 IPA，也没有再次运行 CoreMark、Docker 或
Firefox 全量验收。宿主环境为 Linux x86_64，不能代替 Apple ARM64/iPad 验收。

## 相同 ELF 的定向对照

| CPU | worker FD 操作数，前 → 后 | cohort 轮数，前 → 后 | 循环 ms，前 → 后 |
| --- | ---: | ---: | ---: |
| 1 | 0 → 0 | 0 → 0 | 0.697 → 0.643 |
| 2 | 256 → 448 | 195 → 3 | 6.315 → 0.502 |
| 4 | 256 → 448 | 195 → 4 | 6.684 → 0.585 |

前后 service_requests 都为 0：旧路径是结束 cohort 后执行，而不是 Event::Service。
因此不能只看服务消息数判断迁移完成。变化由精确 worker 调用计数、跨线程结果与
cohort 次数共同证明。计数现在包含 F_GETFD/F_GETFL/F_SETFD/F_SETFL/FIONBIO 和纯
CLOEXEC range，包含错误完成，不包含创建/dup/关闭。

计时只是一轮控制密集用例，排除线程创建；它不证明普通程序、fork 或整个系统的
提速倍数。未改动的 CPU/GPU/既有 io_uring 验收复用之前的结果。

原始输出位于 target/worker-fd-table-checks，原生/ARM64 fixture、前后 runner 和源码
SHA256 记录在 JSON。兼容清单只更新 25/436 的说明和来源，318 项状态基线保持原样。
