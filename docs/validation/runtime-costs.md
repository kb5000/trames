# CoreMark 退化和默认 Docker 开销

2026-10-18，运行时基线 `d492bb9`。用户报告同一 CoreMark ELF、同样命令，
iPad 得分从此前接近 600 降至 400 多；本次未取得新的完整真机日志、App build、
热状态或设备函数采样，不将开发机的结果当作该设备的精确归因。

## CoreMark：16 KiB 页组的软件回退

固定官方 CoreMark `1f483d5b8316753a742cbf5590caf5bd0a4e4777`，用原版签名
Alpine GCC 14.2.0-r6、musl-dev 1.2.5-r12、Make 4.4.1-r3，在 guest 内执行
`make PORT_DIR=linux compile`；之后全部运行复用同一个动态 ELF 和安装盘。
不修改算法，不用 NativeCall 替代它。性能 seeds 为 `0 0 0x66`，2000 字节、
单线程、固定 15000 次迭代、1 guest CPU。所有计入样本均通过 CRC 且计时超过
10 秒。初次 7000 次迭代仅 9.167 秒，未满足报告时长，没有计入结果。

宿主为共享 x86_64 Linux、实际 4 KiB 页，固定宿主 CPU 0；所有 runner 使用
同一 nightly-2026-10-16、优化 release、thin LTO、overflow checks、调试符号。
构建/其他测试不与计时重叠。两轮交替顺序为旧/新、新/旧；样本很少，不建立
置信区间或 iPad 通用加速比。完整输出、指令计数和输入 SHA256 保存在 JSON。

| 对照 | 基线中位数 | 对照中位数 | 说明 |
| --- | ---: | ---: | --- |
| pre-MMU `538232a` / 当前，实际 4 KiB 页 | 19.549 s | 19.778 s | 当前约慢 1.2%，没有复现三成退化 |
| 当前 / 修复，强制协调器按 16 KiB 分组 | 26.2525 s | 21.8725 s | 修复后约快 1.200× |

[版本对照](runtime-costs/coremark-comparison.json)不是仅改变一个变量的 MMU 微基准：
两个提交之间也有其他功能变化。第二组只在诊断源码中将 `Arena::default` 的
granule 改为 16384；宿主本身、指令集、系统调用和 worker 页粒度没有变成
iPad。它用来复现 4 个 guest 页组成一个宿主页时的分组开销，**不是原生 ARM64
或 iPad 测量**。[16 KiB 对照](runtime-costs/coremark-host16.json)保留两个样本：
修复前 568.05/574.73 分，修复后 686.56/685.03 分。

找到的原因是 `prepare_mmu_write`：只有 ELIGIBLE 被视为已准备。普通匿名 RAM
若因为页洞或混合权限无法使用 CPU alias，每次 store 都再次构建页组、查询
页表、分配临时 frame 列表，而页洞和权限没有变化。iPad 常用 16 KiB 宿主页，
所以小堆、边界页和相邻不同权限页更容易触发这条路径；4 KiB Linux 不易暴露。

修复为页组缓存 SOFTWARE 判定。普通单页 store 复用结果；map/protect/unmap/
discard/remap 仍重新计算，fork 仍先退休 aliases。软件路径继续检查实际权限、
backing/EOF、COW、代码失效和并行提交，没有扩大 guest 权限或添加锁。
protect 失败也缓存软件回退，后续映射边界会重试；首次 host window 分配失败
仍走原有软件路径，没有将失败指针缓存为可用 alias。

专门的失败回归在修复前观察到“map 准备 1 次后，1000 次 store 又准备 1000 次”；
修复后无重复准备，同时验证补齐页洞后可恢复硬件 alias、混合权限不会泄漏。
[失败日志](runtime-costs/mmu-red.log)和 [61 项内存检查](runtime-costs/mmu-green.log)
保留。准备计数只在 `cfg(test)` 编译，生产访存不增加计数器。

这已经证明存在可修复的额外开销，尚不能证明它解释全部真机退化；修复后的
16 KiB 诊断结果仍慢于本机 4 KiB 路径。实际设备复测及后台任务/调度、软件
回退重复 TLB 查询、生成代码/寄存器压力等仍需后续证据。
合并本轮两个修复后的最终 runner 另做一次相同 ELF 的
[4 KiB 冒烟对照](runtime-costs/coremark-final.json)：15000 次、19.563 秒、766.75
分、CRC 通过；不是新的多样本速度结论。

## Docker：只采样创建和 exec

手动入口 `tests/performance/docker-lifecycle.py` 复制一个已经装好原版 Docker、
并已 import Alpine 镜像的 store，在私有副本中运行默认 dockerd/containerd/
runc/overlay2。脚本用宿主收到阶段标记的单调时间统计 daemon、create、exec；
不设置 GOMAXPROCS、nofile、runtime 或安全/网络覆盖参数。它不是重复的完整
容器验收，不调用宿主 Docker，也不下载 Registry 镜像。

```sh
cc -shared -fPIC -O2 -std=c11 -Wall -Wextra -Werror \
  tests/performance/pc-sampler.c -o /tmp/trames-pc-sampler.so -ldl
CARGO_PROFILE_RELEASE_DEBUG=1 cargo build --release -p trames-cli
python3 tests/performance/docker-lifecycle.py \
  --runner target/release/trames-run --store /path/to/prepared-store \
  --image default-probe --sampler /tmp/trames-pc-sampler.so \
  --output /tmp/trames-docker-profile
```

`results.json` 保留输入指纹、阶段/累计指标及未完成边界；超时不记为成功。
PC 采样沿用 Linux `ITIMER_PROF`，覆盖所有宿主线程的用户 PC，包含 daemon
启动和清理，使用内联符号栈分类；不是硬件周期、阻塞等待或 iPad Instruments。
原本已通过的网络、overlay 存储、挂载和 seccomp 验收按原范围复用。

### 本轮采样和修复

实际对照使用 [固定脚本](runtime-costs/docker-profile.sh)，从相同安装盘各复制
一份；4 guest CPU、原版 Docker/containerd/runc/overlay2、已有 Alpine image。
该先行脚本用 guest 的 `date +%s` 记整秒阶段时间；它由真实宿主时钟支持，
没有修改 guest 计时。上面的可复用入口后来改用宿主单调时间收集阶段标记。
本轮两次依然使用逐字节相同的先行脚本，不混用两个计时入口。

| 阶段 | 修复前 | 修复后 | 前 / 后 |
| --- | ---: | ---: | ---: |
| daemon 启动 | 16 s | 15 s | — |
| create/start | 435 s | 80 s | 5.44× |
| exec | 205 s | 39 s | 5.26× |
| 整段宿主 wall time | 664.048 s | 140.814 s | 4.72× |

创建、exec 输出、正常退出、清理均成功。只有一次先旧后新的配对，不将它视为
跨机器/跨负载的稳定加速比；并发任务和 deadline 行为会改变退休指令数及重试
次数。[输入指纹/累计指标](runtime-costs/docker-comparison.json)、
[修复前日志](runtime-costs/docker-before.log)、[修复后日志](runtime-costs/docker-after.log)
完整保留。两次都没有其他本任务的构建、测试或计时任务重叠。

基线总计 664.004 秒，其中系统调用协调线程计时 613.717 秒，CPU slice 累计
20.399 秒、快照准备/提交 8.044 秒、idle 1.025 秒。并行 CPU 计时可能重叠，
这些计时不是互斥硬件周期桶，不能简单相加。触发了 **101621 次 proc 刷新**。
每次刷新为全部 task 调用 `Memory::regions`，重复遍历每个地址空间的所有页，
即便共享 VM 且映射没有变化。采样共 166497 个 PC，内存相关分类约 69%；
明确包含 `Memory::regions` 内联栈的有 57101 个（34.3%）。优化器合并后的
泛型 BTree 遍历符号无法全部精确归到调用者，不把约 69% 全写成 maps。
[原分类](runtime-costs/docker-profile-pc.json)和
[内联栈复核](runtime-costs/docker-inline-profile.json)保留这一界限。

修复在协调器缓存格式化后的映射范围/权限，以实际 Memory owner identity 和
映射/代码 generation 为 key。同 VM 的线程复用，普通数据写不使它失效；
map/protect/reserve/unmap/remap、fork/exec 和 generation wrap 保持刷新。
按实际内存 owner 区分已退出 leader 的空 Memory 与仍在运行的同 vm_id 线程；
每次刷新清退已不存在的 owner，缓存不会随历史 fork 数一直增长。
不缓存 argv 字节、FD 路径/位置、用户身份、cwd、namespace 或统计计时。

两个[定向回归](runtime-costs/proc-cache-final-tests.log)覆盖共享保护变更、
PROT_NONE 保留/提交/unmap、fork 独立权限、退出 leader、实时 argv/FD/身份及
缓存清退；受影响模块[严格 Clippy](runtime-costs/clippy-final.log)通过。
内存 61 项结果复用；将新增测试适配 64KiB 主机后仅重跑
[该项回归](runtime-costs/mmu-final-case.log)，没有再重跑全部通过项。
随后按所有者要求构建 [1.0.0 build28](ipad-1.0-build28.md)：
[Apple CI](https://github.com/kb5000/trames/actions/runs/39130864515)通过内存库61项、
内存集成4项与proc-map缓存2项，重新编译当前ARM64 iOS release应用；未变
模块、UI与guest产物复用。没有新增实机性能结果。

修复后仍有 86533 次 proc 刷新，syscall 协调线程时间为 102.536 秒。
[35261 个后续 PC 样本](runtime-costs/docker-after-profile-pc.json)中内存相关分类
降至约 10.2%，VFS 路径/BTree/字符串比较成为可见热点，约 42.1% 样本在未展开
符号的宿主库内。下一步应减少无关的全进程 proc 快照和 FD 路径反查，并对
宿主库采集调用栈；不能把全部库内样本直接归成 malloc、memcpy 或磁盘 I/O。
本轮到此收敛，不继续增加配置覆盖或重复整套 Docker 验收。
