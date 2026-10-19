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

后续同一候选：目录stat/nlink和getdents跳过深层后代，只枚举直接子项，并保持任意
字节名的词典序（包括component-、component.、component/、component0边界）。
串行调度在没有其他可运行peer时采用最多65536条guest指令的有限slice；存在ready peer
保持4096，syscall/native返回仍走原流程。减少每4096指令重复PTY/control/等待任务检查，
没有添加逐指令namespace检查、共享锁或放宽guest权限。

默认PID namespace本来已有local/global直接映射；指令/内存热循环不查询namespace。
4KiB本地单程序CoreMark约760分，开启8核配置/PTY/Wayland约752；额外等待shell约670。
这是调度/会话形态差异，尚不证明解释器本体存在与iPad同样的三成退化。
16KiB诊断只改变协调器页组（实际Linux仍4KiB/x86），不能冒充Apple硬件MMU。
生产多终端ABI测试入口见tests/performance/session-coremark.py；原始签名Alpine GCC
与固定上游CoreMark准备入口见prepare-coremark.py。

Linux受影响Core56、VFS152项通过（另保留既有4+1项ignore），严格相关Clippy通过。
完整候选Docker、受控Session配对及Apple真实宿主页/ARM64数据正在测量，未记为通过。
