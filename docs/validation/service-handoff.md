# 文件服务交还执行槽与完成入队

> 本文为公开的验证汇总。提及的原始日志、二进制和逐项 JSON 记录保留在开发环境，未随本次源码提交公开。

2026-10-22，接续[阻塞 I/O 交接](io-handoff.md)，将生产 cohort 的文件/FD 服务从
“宿主 worker 阻塞收回复”改为“请求持有原任务，完成直接排回共享队列”。协调角色
仍负责文件对象、权限及完成提交。本地未提交工作树，没有推送、IPA 或 release。

## 本轮范围

- 普通 FD 查询、标量/向量/定位文件 I/O、sync 与现有 Unix stream 服务共用新的请求
  生命周期。请求持有原 Box<Job>，CPU、View、快照和翻译缓存保持原分配，不逐次将
  约 6 KiB View 搬入/搬出消息信封。完成方只写请求独占的 CPU，不访问 Process 的
  正在运行状态；guest 访存仍使用同一个执行 View 与原 Linux helper。
- 提交前释放宿主执行槽；同一槽可处理另一个任务，或继续原生计算。完成后在队列
  锁下直接重新入队，没有 per-worker 回复 channel、忙等、轮询完成邮箱或新增服务
  线程。请求提交仍经过原事件通道，VFS 效果仍由原所有者处理，没有把 Rc/RefCell
  宣告成 Send。既有最多两个磁盘 I/O 线程保持不变。
- outstanding service 和 ready/running 同步维护，所有 guest 暂时等待时不会误报
  quiescence。完成与 stop 共用条件变量；停止后已执行的效果仍提交，未执行请求
  返回原 syscall，异常丢弃则释放 View、报告失败并唤醒宿主执行槽。
- cohort 结束不仅等待 worker 停止，还显式收齐请求；只有全部 View 归还后才能提交
  backing 并执行生命周期/映射变化。SVC 与预算仍只记一次，等待时间不计入 CPU 用时。
- 释放执行槽后，guest 等待者数可以超过 host CPU 数，因此明确限制已提交磁盘读取
  数不超过配置 CPU 数（最多 32），每个读取缓冲至多 1 MiB。其余等待请求只持原任务
  和参数，资源完成时重新准入，不预读。小读取和元数据不受大读取槽限额阻塞。
- 同一 OFD 的顺序 read/lseek、同 backing 的写入/sync 保留原冲突判断；定位读取可
  重叠。FD 权限、映射、RLIMIT_FSIZE、SIGPIPE 和 partial I/O 没有复制成第二套算法。
- 新增 worker_service_waits 和 peak_disk_reads 指标；worker_task_resumes 包含服务
  完成后的重新就绪，service_wait_seconds 是请求等待时间，不是 CPU 执行时间。

旧的单任务 dispatch/reply transport 仅在 cfg(test) 中保留，用于既有验证和通信成本
实验，生产只用 cohort 路径。其测试专用大信封保持原按值 View，因此枚举尺寸 lint
只在测试下作局部说明；生产请求没有该变体或额外回复 channel。

## 正确性与资源边界

- core Release 125 项通过，2 项既有手工项目忽略。单槽两任务测试同时持有两个未回复
  请求，先回复第二个并继续提交下一个；检查前后 View 地址一致。停止后提交三次
  已完成结果，恰好退休 4 条指令，剩余预算 96，不重放 syscall。
- 单槽原生测试让第一个任务的服务请求保持未回复，同一宿主 worker 继续完成另一个
  任务的真实 native memset 并返回边界；随后交还已完成结果。共 7 条指令，预算 993，
  写入 128 个 0x72 字节，原生 worker call 为 1。
- 新检查覆盖请求 None 回退、异常丢弃唤醒闲置执行槽、Pool 关闭、停止期间完成提交，
  以及一槽磁盘限额下第二个独立定位读取排队/重新准入。共享文件偏移和输出内容正确。
- 严格 Clippy（core/CLI/FFI、tests、Release、`-D warnings`）、Release CLI/FFI 构建、
  全工作区 fmt 与 diff 空白检查通过。没有更改 CPU/MMU、Linux/VFS/GPU 的实现；这些
  模块复用上一轮已通过结果，没有再次运行完整工作区、CoreMark、Docker 或 Firefox。

## 真实 musl 集成

新 fixture 使用同一个 ARM64 静态 ELF：六个读取线程各做四次 64 KiB 读取，另一线程
做 32 次原生 1 MiB 填充。校验 24 条唯一记录、内容、总和 276、原生 checksum
c0c615f0，以及顺序/定位读取后的文件位置。独立 host C 程序提供断言与输出 oracle。

pread/preadv/read 三种模式 × pinned/spread × 配置 2/4 CPU，共 12 组全部通过。
pinned 模式把所有 guest 线程限制到 CPU 0，仅一个宿主 guest 执行槽可用：

| 调用 | 配置 CPU | guest 执行峰值 | 磁盘在途峰值 | worker 原生填充 |
| --- | ---: | ---: | ---: | ---: |
| pread | 2 | 1 | 2 | 32 |
| pread | 4 | 1 | 4 | 32 |
| preadv | 2 | 1 | 2 | 32 |
| preadv | 4 | 1 | 4 | 32 |
| read，共享偏移 | 2 | 1 | 1 | 32 |
| read，共享偏移 | 4 | 1 | 1 | 32 |

在途包括已提交到既有磁盘 I/O 池的作业，不代表四个磁盘宿主线程。native 共 32 MiB，
fault/reject/fallback 为 0。共享偏移的 read 只有一个在途，避免多个请求读取相同旧
位置；这与独立定位读取的并发不同。测试不宣称应用提速或四核计算扩展性。

既有 epoch-services 只重查受影响的 2/4 CPU：metadata/file/vector/Unix stream、
private/shared mapped read 与 bulk-parallel，共 14 组通过；共享可写映射仍正确回退
joined 路径。使用原有 ELF 和 host C oracle，没有因 harness 增加 CPU 选择参数重建
fixture，也没有重新执行未变的单 CPU/CPU 差分项目。

`service-handoff/results.json`（本地验证记录）记录复用范围。未执行新的
QEMU、Apple ARM64/16 KiB 页或 iPad 真机验收，也没有读取/复制私人 Firefox 诊断包。
本轮做功能与资源上限检查，没有新增前后跑分，不从测试时间推断性能收益。

## 剩余工作

请求的文件对象准入、offset/atime/通知与磁盘完成 scatter 仍在协调所有者执行。
下一步继续共享文件/OFD 状态，使允许的短文件调用也在当前 worker 直接完成，再让
文件完成提交只同步相关对象。socket/PTY、GPU、共享 key futex、生命周期/映射/权限
仍有独立边界；冻结 cohort 的 View 准备和周期检查也尚未全部拆开。

复现新功能检查：

```sh
bash scripts/build-service-handoff-fixture.sh
cc -O2 -Wall -Wextra -Werror -pthread -DTRAMES_HOST_ORACLE=1 \
  tests/fixtures/service-handoff.c -o target/service-handoff-host
cargo build -p trames-cli -p trames-ffi --release
python3 tests/integration/service-handoff.py --runner target/release/trames-run \
  --oracle target/service-handoff-host --output target/service-handoff-results
```
