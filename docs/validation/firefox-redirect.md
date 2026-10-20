# Firefox：单核离线页面已通过，默认多核仍待排查

后续：[文件变更域优化](firefox-backing-domains.md)减少无关文件页的快照扫描，
四核低刷新率诊断组也已通过离线 HTML / JS / canvas；默认刷新率仍待解决。
以下保留优化前的调查记录。

2026-10-20，继续[文件通道调查](firefox-open-document.md)。原版 Alpine ARM64
Firefox 142 首次完成离线页面验收：目标 HTML 加载、JavaScript 修改标题，并通过
Wayland SHM 画出完整的绿色 canvas。文件打开、通道重定向、子侧解析和页面脚本
这一整条链路在单核诊断组中已经走通。

通过组仍有明确限制：单核、显式低刷新率、默认内容沙箱和 Wayland proxy，
总/单进程内存各 4 GiB，网络/GPU 关闭；宿主是 Linux，不属于 iPad 真机、联网、
EGL 或 GPU 验收。没有修改 Firefox ELF，也没有把诊断偏好改为产品默认。

默认四核、以及降低刷新率的四核组仍未通过页面验收。关闭窗口级 Wayland Vsync
能够推进到目标父侧加载，但没有解决剩余等待，因此不能把 Vsync 当成全部原因。
各组配置、源码和诊断方式不同，不据此宣称某项改动有固定加速比。

跟踪还发现并修复了独立的 [ARM64 SIGSYS 参数问题](firefox-seccomp.md)：
seccomp TRAP 不应把第一个参数寄存器 x0 替换成系统调用号。修复保留过滤器，
没有绕过 Firefox 或 Docker 的沙箱。Core 检查及真实 ARM64 fixture 的单/多核
测试通过；具体修复见提交 `3c7b530`。

四核主线程仍在同步渲染等待、IPC 和 GTK 事件处理之间推进，并非一直停在同一条
CPU 指令。后半程执行推进变慢，需要继续把协调器的内存 View 准备/提交、IPC
系统调用和 guest 执行分开采样。对安装 seccomp 的线程，现有代码还会关闭 worker
快速系统调用；不可变过滤快照是后续值得研究的方向，尚未实现或验证收益。

原始日志、线程栈、截图和运行转储按所有者要求保留在本地，本文只记录工程结论。
临时诊断源码已恢复，没有重新运行未变模块的完整验收，也没有构建 IPA。
[Issue #33](https://github.com/kb5000/trames/issues/33) 保持开放。
