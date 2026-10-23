# 共享 PID namespace 身份与映射

2026-10-24，第 39 批。机器记录见 [JSON](shared-pid-namespace-summary.json)。

实际 PID namespace 由 Rc 改为 Arc，proc 快照和注册表使用弱 Arc。默认 namespace
的 PID 正反转换保持直通，不取得映射或注册表锁。init/dead 使用 relaxed 原子；
嵌套 namespace 的编号和映射按对象同步，不改变 CPU/native 的普通访存路径。

分配时按根到叶取得祖先映射锁，先检查全部死亡状态和编号上限，再提交各级映射。
与 init 退出竞争时，要么完整分配，要么失败且不消耗祖先编号，不会留下半份映射。
注册表操作不在持有这组锁时执行。最终引用可以在 worker 释放，弱身份随后失效。

这一步是实际共享所有权迁移，没有新增 worker syscall，不能直接推导应用提速。
fork/exit、全局任务编号分配、init 退出的任务清理和映射裁剪仍在任务生命周期提交
边界；裁剪要求调用者提供该边界下的存活任务集合，不能使用过期快照删除并发分配。
其他 namespace、完整 FD/OFD、Node/页布局与 VM/task 生命周期仍未全部迁出。

验证结果：

- 9 项 release Rust 检查通过，含并发分配、祖先回滚、死亡竞争、配额回收、最后
  worker 引用释放、范围锁可见性与 proc mount 身份。默认空间测试故意占住两个锁，
  另一线程仍完成 PID 转换、分配检查和默认锁可见性查询。
- ARM64 原 PID namespace fixture 在内存/磁盘 × 1/2/4 CPU 的 6 组配置通过，覆盖
  clone/unshare/setns、proc 视图、信号/waitid、线程、孤儿收养和 init 退出。
- 原范围锁 fixture 补查内存双核、磁盘四核，均通过。改动前 runner 的 PID/范围锁
  各一组也通过；不是仅以新实现验证自身。
- Release CLI/FFI、严格相关 Clippy、格式及差异空白检查通过。

未重新运行未修改的日志、UTS、映射、io_uring、CPU、网络和图形套件；复用既有
通过结果。没有修改宿主 namespace，没有制作 IPA。原始记录留在本地
`target/shared-pid-namespace-checks/`，本批不声称 CoreMark、Docker 或 iPad 加速比。
