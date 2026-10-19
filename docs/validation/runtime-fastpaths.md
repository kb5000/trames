# 默认空间与 Docker 运行路径性能

用户在1.1.0上报告docker run能够启动但很慢，原来约600分的CoreMark仍约400。
本轮不改guest算法/时钟，不做JIT，也不以x86开发机分数代替iPad复现。

当前本地采样：相同默认Docker/Alpine已安装磁盘的私有副本，4 guest CPU，
release优化+调试符号，ITIMER_PROF顶层PC采样，未与构建/其他测试并行计时。
基线daemon12.952s、create/start74.151s、exec32.775s、全程126.023s。
累计syscall计时91.891s；不同线程/等待计时不能直接当互斥周期桶相加。
31756个CPU样本中VFS44.3%、宿主库等exe外40.9%、guest memory10.2%、
协调器/内核2.8%、指令解释0.5%；未将exe外样本强行归为VFS。
明确含Mount::key_of内联栈的样本13.7%，还有共享泛型BTree遍历，无法完全归因。

第一步仅验证inode路径提示（每inode一个，不持有其他inode、不写磁盘）：
命中必须确认当前FS路径仍指向同一对象且在所需bind根内。rename/unlink/replacement/
跨bind-root会失效再查找，保留真实权限/namespace语义，不绕过目录权限。
此单项一次配对：create74.151→52.397s，exec32.775→25.418s，全程126.023→99.537s。

最终候选：目录stat/nlink和getdents跳过深层后代，只枚举直接子项，并保持任意
字节名的词典序（包括component-、component.、component/、component0边界）。
串行调度在没有其他可运行peer时采用最多65536条guest指令的有限slice；存在ready peer
保持4096，syscall/native返回仍走原流程。减少每4096指令重复PTY/control/等待任务检查，
没有添加逐指令namespace检查、共享锁或放宽guest权限。

默认PID namespace本来已有local/global直接映射；指令/内存热循环不查询namespace。
4KiB本地单程序CoreMark约760分，开启8核配置/PTY/Wayland约752；额外等待shell约670。
这是调度/会话形态差异，尚不证明解释器本体存在与iPad同样的三成退化。
没有重做namespace实现或缓存权限决定；优化共同VFS路径同时惠及默认空间和容器。

最终候选的同输入Docker配对（各一次，镜像已存在，不包含Registry拉取）：

| 阶段 | 1.1.0基线 | 最终候选 | 旧耗时/新耗时 |
| --- | ---: | ---: | ---: |
| daemon启动 | 12.952s | 13.237s | 0.978× |
| create/start | 74.151s | 38.216s | 1.940× |
| exec | 32.775s | 16.683s | 1.965× |
| 全程含清理 | 126.023s | 73.634s | 1.711× |

同一采样分类器核对基线分类完全一致后，最终18661个样本中VFS23.8%、
exe外53.1%、guest memory17.3%、协调器/内核3.3%、解释器0.7%。
这是整个生命周期的排他顶层PC分类，不是create单阶段的调用图，也不是硬件周期。
`key_of`明确内联归因样本从4347降到15，但新`path_in`仍有1351个样本；
共享BTree泛型和宿主库的完整调用来源尚未展开。
累计syscall91.891→44.233s，CPU slice13.623→13.769s。
四个guest CPU保持启用，实测峰值四核；未强制单核、减nofile或换runtime。
剩余优先方向是系统调用/VFS路径与宿主库归因，不能把不同线程的累计计时相加为百分比。

受控生产Session ABI配对：原版相同guest ELF、15000迭代、相同输入manifest
`408a8b970346fcb50bf3951a3e4769bed11f2243b52d77b95853adc23b3c7aa3`，
分数598.444→707.714（1.183×），时间25.065→21.195s，CRC与>=10s均通过。
该16KiB诊断只改变协调器页组（实际Linux仍4KiB/x86），不能冒充Apple硬件MMU。
早期未冻结输入manifest的探索记录不纳入此配对结果。
生产多终端ABI测试入口见tests/performance/session-coremark.py；原始签名Alpine GCC
与固定上游CoreMark准备入口见prepare-coremark.py。

Linux受影响Core56、VFS152项通过（另保留既有4+1项ignore），严格相关Clippy通过。
[精确源码Linux CI](https://github.com/kb5000/trames/actions/runs/39187274654)同样通过上述检查，
生产三终端PTY、共享状态、private cwd/env、resize、Ctrl+C、输出背压、关闭/退出/重开与
快速取消通过。代码源码`42628d2365e5f60936b08e0df679684a93c64f68`；基线代码同
不可变发布`b25feb43ba019850511b440af34e9595f1b2e97c`，本地基线仅多文档提交975027e。

[Apple CI](https://github.com/kb5000/trames/actions/runs/39187274741)完成：macOS 15 ARM64，
`hw.pagesize: 16384`，原版签名Alpine GCC14.2 `-O2 -lrt`，固定上游
`1f483d5b8316753a742cbf5590caf5bd0a4e4777`。原始guest GCC编译一个ELF，两版本
均复制同一只读输入store，通过真正生产多终端FFI入口运行；release thin-LTO、符号级别1。
先完成构建/测试再计时，按旧、新、新、旧顺序运行，每次15000迭代且CRC/至少10s通过：

| 次序 | 版本 | iterations/s | guest耗时 |
| --- | --- | ---: | ---: |
| 1 | 旧 | 312.891 | 47.940s |
| 2 | 新 | 379.853 | 39.489s |
| 3 | 新 | 389.095 | 38.551s |
| 4 | 旧 | 294.060 | 51.010s |

分数中位数303.476→384.474，1.267×；只有每版两个样本，没有置信区间。
这是云端Mac的吞吐，不能用绝对分数对比iPad或Linux，更不是硬件周期。
Apple Core56、VFS151（Linux多一项平台专用测试）通过，既有4+1项ignore保留，
生产多终端交互/取消也通过；无新IPA或发布。CI artifact ZIP的SHA-256与各项CRC验证
通过，原始结果、编译输入哈希、全部四条stdout与CI日志保留在同目录。
尚未在用户iPad复现400分或确认恢复历史600分；未把此项性能回退Issue关闭。
[原始日志与哈希](runtime-fastpaths/results.json)保留单次测量、宿主架构与诊断范围。

复测入口（输出目录必须不存在；store先冻结，执行时各自复制，禁止与构建并行计时）：

```sh
python3 tests/performance/docker-lifecycle.py --runner target/release/trames-run \
  --store target/docker-default/store --output target/docker-profile-new
python3 tests/performance/prepare-coremark.py --runner target/baseline/release/trames-run \
  --output target/coremark-inputs
python3 tests/performance/session-coremark.py --library target/release/libtrames_ffi.dylib \
  --store target/coremark-inputs/store --output target/session-profile-new
```
