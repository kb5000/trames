# Docker 启动时的宿主虚拟地址压力

2026-10-18，用户反馈1.0.0 build28在containerd成功启动后App退出；guest总预算和
单进程预算均为4GiB。提供的崩溃记录确认 `trames-cpu-1` 在 `Cache::page` 分配
失败，经 `rust_oom` 主动 `abort`，信号为SIGABRT。最后一次分配处不等于缓存泄漏
根因，不能把虚拟地址占用当作实际物理内存。原始设备标识及崩溃文件不入库。

## 问题和修复

旧映射对象把16MiB物理RW区域和16MiB CPU别名绑定在同一个Arc生命周期中。
物理worker池创建了不使用的CPU别名；Frozen CPU窗口又创建不使用的物理区域；
fork保留物理页时，也保留了退休CPU地址预约。少量存活页、稀疏Go地址和重复
快照会放大虚拟地址需求，而guest的committed页预算没有统计这些宿主映射。

修复将物理 `Mapping` 与 `CpuMapping` 分开。CPU窗口退休即可释放，物理页仍按
原有Rc/Arc/fork/快照所有权保留。Frozen视图只预约CPU地址，物理池只映射RW
存储；普通窗口由原来的16MiB缩为1MiB，降低稀疏地址和旧slot保留成本。
最后一个页/可用组被移除后释放空的协调器/worker窗口。

所有MMU物理窗口和CPU地址预约合计最多1GiB，创建和销毁时用RAII计账，失败也
回收预算。达到上限或OS拒绝时复用原有软件访存，不改变guest内存预算、权限、
线程原子性或错误语义。失败窗口在普通store期间不重试，映射边界可以重试。
热访存不增加预算原子操作；可用区域继续使用硬件MMU。

## 定向验证

8个guest CPU、同一份已安装原版Docker的磁盘私有副本，共享x86_64 Linux、
实际4KiB宿主页。初次前后对照均使用默认1GiB总预算/512MiB单进程；只执行
到 `API listen on` 后由宿主结束进程。它们不是完整生命周期或速度跑分。

| 启动观察 | 修复前 | 修复后 |
| --- | ---: | ---: |
| MMU memfd视图峰值 | 14,092,861,440 bytes（13.125GiB） | 1,060,024,320 bytes |
| RSS采样峰值 | 1,185,652 KiB | 1,011,852 KiB |
| dockerd API就绪 | 是 | 是 |

Linux统计仅累加命名RAM memfd视图，包含别名；未别名的PROT_NONE CPU预约没有
计入此采样，但也由全局1GiB预算覆盖。不要把该数字与iPad vmSummary直接相除。

另按用户设置执行4GiB总预算/4GiB单进程、8CPU，将Linux宿主RLIMIT_AS限制到
2560MiB：默认managed containerd/runc/overlay2启动，`docker info`返回
`overlay2 8 4294967296`，daemon正常关闭、guest正常退出。MMU视图峰值
1,057,726,464 bytes，RSS峰值1,284,156KiB。脚本只验证本次失败阶段，不重复
容器create/exec、网络隔离、overlay持久化全套验收。

初版角色拆分通过内存库62项与translation4项；最终版本的17项MMU、4项translation
及memory/core/CLI严格Clippy通过，未变的其他检查复用。新增回归验证预算耗尽和
回收、别名数据一致、fork保留字节但释放CPU视图、最后unmap和worker组移除回收
窗口，以及资源回退不在每次store重试且不扩大写权限。

[启动脚本](../../tests/integration/docker-startup-memory.py)支持准备好的store私有副本，
也能自动下载原版Alpine签名Docker包准备新store；不启动宿主Docker。Apple CI
补充实际Darwin/ARM64内存检查和全新store的Docker启动，再构建当前iOS Release。
它仍不能代替Sideloadly签名App的iPad实际验证。

原始采样和日志在[数据目录](mmu-va-pressure/)，[机器可读摘要](mmu-va-pressure.json)
绑定运行时代码patch与输入指纹。修复前后各一次启动，只用于确认资源放大/边界，
不据此宣称通用性能加速比。

## Apple ARM64 验证

[build29 CI](https://github.com/kb5000/trames/actions/runs/39134234642)的定向检查通过。
实际Darwin ARM64、16KiB宿主页，从新的Alpine store安装18个原版签名包；
8guest CPU、4GiB总预算/单进程预算，默认dockerd→managed containerd/runc、
overlay2正常启动，`docker info`与正常退出通过。启动/info/关闭42.943秒，
RSS采样峰值1,218,544KiB；不是iPad用时，也未在Darwin施加Linux的RLIMIT_AS。
[证据artifact](https://github.com/kb5000/trames/actions/runs/39134234642/artifacts/12139322416)
已下载核验ZIP digest和CRC；包、runner和脚本指纹见原始Apple记录。

当前iOS Release [build29安装包](ipad-1.0-build29.md)已构建、下载检查通过；真机复测待用户进行。
