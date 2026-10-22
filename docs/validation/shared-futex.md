# 拆分协调器：共享 futex 队列

> 本文为公开的验证汇总。提及的原始日志、二进制和逐项 JSON 记录保留在开发环境，未随本次源码提交公开。

2026-10-22，接续[worker 本地 I/O](worker-local-io.md)。本轮拆分等待表和 private wake，
**协调线程、全体 epoch 调度边界仍保留**。未构建 IPA，没有应用提速数据。

## 已完成

- 持久共享等待表替代逐轮扫描进程、分配 private bitset 快照。private key 使用
  VM + 去 tag 地址，shared key 沿用原共享 backing 身份。64 个桶分别同步，不锁整个
  Kernel/VFS；普通 guest 访存与原生函数不经过这个表。
- WAIT 在同一 key 桶锁内比较并入队；阻塞的是 guest task，不在 host worker 上等待
  futex 条件或回复。等待结果用单字段原子 CAS 仲裁 wake/timeout/cancel，使用 relaxed
  次序；队列发布由对象锁完成，不额外加强 guest 普通读写的内存次序。
- private WAKE/WAKE_BITSET 在当前 worker 直接检查并认领真实等待者，返回实际数量。
  不是只处理空唤醒，也不再把真实 wake 的执行提交协调 RPC。seccomp 非 ALLOW、trace、
  其余 futex 操作仍保留旧执行边界；先验证地址/对齐/count/bitset，错误没有队列效果。
- 真实 wake 将 ticket 放进共享 ready 队列并设置现有 stop 标志，以现有 Done 事件
  请求调度；没有新线程或轮询通信。全体返回后先发布 ready，再处理已返回的
  WAIT/REQUEUE/exit/signal。该阶段唤醒新任务**仍需要全体调度边界**。
- 注册的最终所有者释放时取消并移出 key，无效地址比较、超时、信号、退出不会留下
  幽灵等待者。ready 校验 ticket 身份，旧 PID 通知不会命中新注册。停止任务可保留
  原等待；退出时清除保存的停止状态，避免僵尸继续占用等待队列。
- REQUEUE 只查相关队列，保留原 ticket、bitset 和 deadline，按桶序拿锁；不扫描全部
  进程。取消/过期后无可移动项时不留下空目标 key。clear_tid 与 shared wake 复用同表。
- worker_futex_wakes 记录有效本地 private wake 调用（含零返回），
  worker_futex_woken 记录实际唤醒 ticket。计数只写 worker 私有 Cell，汇总时合并。

## 验收

Linux x86_64、Release、静态 ARM64 musl。独立 host C 输出 oracle 与同一 guest ELF 的
1/2/4 CPU 运行比较，覆盖 16 轮三等待者 bitset wake、CMP_REQUEUE、超时/参数负例，
以及 fork + MAP_SHARED 的跨进程 futex。等待流程同时保持一个原生内存填充线程运行。
原生循环次数取决于调度，仅验证正确性，不能比较三组时间或将 4 CPU 配置当成
四个持续满载 CPU。具体路径计数见`shared-futex/results.json`（本地验证记录）。

| 配置 CPU | worker private wake 调用 | 实际唤醒 ticket | 协调 RPC |
| --- | ---: | ---: | ---: |
| 1 | 0 | 0 | 0 |
| 2 | 112 | 48 | 0 |
| 4 | 112 | 48 | 0 |

三组全部通过，输出与 host oracle 一致，原生 fault/reject/fallback 均为零。单 CPU
沿用同线程的原执行位置；原生填充次数及 warm 调度顺序可变，上表不是吞吐比较。

原子竞争用例覆盖 4 个宿主 waker 对 257 个 ticket 的恰好一次认领、512 个 ticket 的
超时/唤醒竞争、VM/共享 key/bitset 隔离。其他用例覆盖比较与入队不漏唤醒、注册克隆
与最后释放、同桶/跨桶 requeue、PID 重用、先 wake 后过期、signal 取消、停止任务
requeue/continue、停止状态退出。worker 测试直接观察 Done 而非 Service，确认返回
实际数量，SVC 只退休一次，不回放已完成的 wake；空 wake 仍连续运行到精确预算。

12 项 futex 检查、8 项受影响的调度预算/停止/信号/退出检查及严格 Clippy 通过，构建 Release
CLI/FFI。原来的 VFS/Linux/CPU/MMU/GPU/UI 源码本轮未改，通过记录复用；没有重跑整个
项目、旧通信微基准或 CoreMark。没有 Apple ARM64/16 KiB 或 iPad 真机验收。

## 下一步边界

WAIT 入队、REQUEUE、shared-key 解析、信号/定时器和进程生命周期仍由 joined Kernel
处理。阻塞 I/O、FD 表/OFD 生命周期及其余 VFS/网络/GPU 服务也还没全部移出协调器。
不能直接删除 join：私有页准入要求全体 View 的所有权成立，映射撤销和 fork/exec
仍需相应的失效协议。下一步拆 task 执行状态和 VM 租约，让阻塞任务释放执行槽，
新 ready 任务能被 worker 接手，再把暂停缩小到相关 VM/资源。

复现（已有 musl cross wrapper；输出目录必须不存在）：

```sh
bash scripts/build-shared-futex-fixture.sh
cc -O2 -Wall -Wextra -Werror -pthread -DTRAMES_HOST_ORACLE=1 \
  tests/fixtures/shared-futex.c -o target/shared-futex-host
cargo test -p trames-core --release futex
cargo clippy -p trames-core -p trames-cli -p trames-ffi --tests --release -- -D warnings
cargo build -p trames-cli -p trames-ffi --release
python3 tests/integration/shared-futex.py --runner target/release/trames-run \
  --oracle target/shared-futex-host --output target/shared-futex-check
```
