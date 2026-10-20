# ARM64 seccomp TRAP 的参数保留

2026-10-20，继续 Firefox 默认沙箱调查。原版 Firefox 142 的诊断日志中，
`openat` 的目录 fd 变成了 56，`readlinkat` 的目录 fd 变成了 78；这些值正好是
对应的 ARM64 系统调用号。追到 `Kernel::filter_syscall` 的 TRAP 分支，发现它在
构造 SIGSYS 信号上下文前，错误地把 x0 改成了系统调用号。

Linux 的 [SECCOMP_RET_TRAP](https://github.com/torvalds/linux/blob/master/kernel/seccomp.c)
先调用 syscall_rollback，让处理器看到原寄存器；
[ARM64 syscall_rollback](https://github.com/torvalds/linux/blob/master/arch/arm64/include/asm/syscall.h)
恢复 `regs[0] = orig_x0`。ARM64 系统调用号位于 x8，x0 是第一个参数。
因此这里保留 x0，调用号继续由 x8 和 siginfo.si_syscall 提供。
未放宽或绕过 Firefox / Docker 的 seccomp 过滤器。

新增 Core 回归分别使用 openat/readlinkat 的调用号，检查信号帧里的全部六个
参数、x8、SIGSYS 元数据和处理器修改返回值后的 sigreturn。
[旧实现失败](firefox-seccomp/unit-before.log)：x0 实际为 56，预期为 -100。

真实 ARM64 C fixture 还在 SVC 后的 SIGSYS 处理器中检查六个非零参数和 x8，
写入返回值 777，并覆盖过滤器叠加、fork/exec、TSYNC、强制信号及进程终止。
[旧 CLI 失败](firefox-seccomp/fixture-before.log)，修复后 1 / 4 guest CPU 均通过，
stdout 与独立的宿主原生 Linux oracle 相同。宿主是 x86_64，ARM64 寄存器断言由
guest fixture 验证；本次没有冒充原生 ARM64 Linux oracle。

本轮验证：Core 88 项通过、1 个手工 benchmark 忽略；严格 Clippy、fmt 和
`git diff --check` 通过。[Core 日志](firefox-seccomp/core-tests.log)、
[fixture 报告](firefox-seccomp/fixture-results.json)、
[Clippy](firefox-seccomp/clippy.log) 保留原始结果。
复用源码未变的 Memory、CPU、Accelerator、VFS、Node 及先前 Apple ARM64/16 KiB
验证；本次没有重新运行 Apple CI，也没有构建 IPA。

该修复有独立的 ABI 回归证据。Firefox 页面加载及多核性能的复测另行记录，
不能仅凭这些沙箱日志认定它是所有页面等待的原因。
