# PTY 的工作线程读写与等待

2026-10-22，第十二批协调器拆分。guest PTY 的标量/向量字节 I/O、只读属性/尺寸查询、
FIONREAD，以及包含 PTY 的 poll/epoll 使用共享终端对象和原任务。设计及保留边界见
[架构说明](../architecture/epoch-services.md)。本批没有修改网络后端或普通 CPU 访存。

## 检查

18 项不同的单元测试通过：PTY/devpts/共享资源 9、原 line discipline 6、Linux ABI 1、
内核终端信号 2。包括后台读/TOSTOP 写无效果回退、SIGTTOU 忽略、Ctrl+C 前缀不消费、
ICRNL/IXON 优先级、packet 流控、echo/CRLF、保留资源不增加打开端点、HUP/EOF/EIO、
通知解锁后重入、向量 I/O、尺寸/属性查询和错误地址不消费输出。

首轮新生命周期测试缺少启用伪设备的 runtime 初始化，修正测试设置后仅重跑该失败项；
其他已通过结果直接复用。Release CLI/FFI 构建、VFS/Linux/Core/CLI/FFI 严格 Clippy
（含 tests）、格式与脚本语法检查通过，没有重复完整 CPU/GPU/网络/浏览器验收。

另运行现有 production FFI 多终端验收：同一个离线 Alpine 中三个独立 shell、共享
文件/进程、独立 cwd/env、尺寸更新、Ctrl+C、输出 backpressure、关闭/退出/重开及
快速取消尚未打开的终端全部通过。使用现有 musl hybrid 产物，未重建未修改的 guest 库。

## 同一原版 musl ELF 的前后对照

静态 ARM64 程序通过标准 openpty 创建 master/slave，两个持久线程往返 64 轮，每次
传输 32 字节并原生 memset 8192 字节；检查完整内容、终端尺寸、最后关闭的 HUP/EIO。
read、vector、epoll ET、ppoll 四种模式各在 1/2/4 CPU spread 和 4 CPU pinned 下
运行修改前后版本，共 32 次 guest 运行，输出全部匹配同一 C 程序的 8 次宿主原生参考。
所有运行服务请求为零，128 次 native memset 无 fault/fallback/rejected。

| 模式 | CPU/affinity | 修改前整组返回次数 | 修改后 | 修改后本地 I/O 完成数 |
| --- | --- | ---: | ---: | ---: |
| read | 2/spread | 147 | 4 | 256 |
| read | 4/spread | 153 | 4 | 255 |
| vector | 4/spread | 141 | 4 | 256 |
| epoll | 4/spread | 193 | 4 | 382 |
| ppoll | 4/spread | 422 | 4 | 382 |
| read | 4/pinned | 133 | 4 | 255 |
| epoll | 4/pinned | 134 | 4 | 382 |
| ppoll | 4/pinned | 261 | 4 | 382 |

所有多 CPU 新路径实际 park 至少 127 次；pinned 将两个 guest 线程固定到 CPU 0，
证明等待者会释放唯一可用执行槽，由对端运行并唤醒原调用。单 CPU 原来的 joined
路径照常通过，两版整组返回计数均为 0。完成数随 cohort 首尾哪些调用落在 joined
路径略有差别，其他调度计数也会随运行变化，不要求这些样本数字固定相等。

这是 Linux x86_64/4 KiB 宿主的结构样本，不能把返回次数的比例当成 Docker 或 iPad
实际加速比；本 fixture 很短，也不据其毫秒级墙钟时间宣称吞吐提升。Apple ARM64/
16 KiB 与 iPad 交互验收尚未执行。信号输入、作业控制、可变 ioctl 和宿主 UI pump
仍保留内核入口，非 Unix-stream 网络后端仍需后续拆分。

可复现入口为 `scripts/build-worker-pty-fixture.sh` 与 `tests/integration/worker-pty.py`，
脚本支持 `--baseline`、`--runner`、`--oracle`、`--output`。完整命令和本地逐项输出
保留在 `target/worker-pty-checks/`；公开摘要只包含统计及源/产物哈希：
[worker-pty-summary.json](worker-pty-summary.json)。
