# 共享可写映射引用

2026-10-23，第 31 批协调器拆分。MappedData 与每个具体 Mount 使用共享原子
writer 计数，FileMapping 持有实际引用租约。删除每页 writer 字段、逐页 writer
扫描及 `writers_cached` 的设置/失效；`has_writers` 变成一次原子读取。
机器记录见 [shared-mapping-writers-summary.json](shared-mapping-writers-summary.json)。

## 所有权与行为

租约分别记录 inode 与实际 mount 域，别名共享 PageBacking 时不重复取得租约。
显式保留的写引用可移动到 worker，在最后释放时直接更新计数，无须 owner 扫描
或缓存刷新。引用计数使用 relaxed 次序，不作为字节发布屏障；每页不额外分配
独立堆租约，也不改变普通 CPU/native guest 访存、页锁或 MMU 路径。

FileMapping 仍按原逻辑尝试回写、清理页面缓存，再释放自身写引用。仍保留的引用
使文件 epoch I/O 保持受限，即使其旧页面缓存已经清空。只读 remount、普通 umount
及懒卸载树回收都检查同一挂载点计数；只有最后释放后才能放行。bind/namespace
克隆沿用原挂载域划分，不把不同 Mount 的计数合并。

此处迁移的是可写引用和 busy 状态，不是完整 FilePage/PageBacking、inode、路径
或映射提交。它们仍有 Rc/owner 生命周期与回写边界，不能仅持有计数租约就操作已
失效的页。写入 epoch I/O 仍会检查其他活跃页面引用；没有宣称所有映射扫描已消失。

## 验证

21 项不同的 release Rust 检查通过，包括跨线程租约释放、多个 inode/挂载域、
共享/私有映射、COW、EOF/SIGBUS、截断/增长、decoder token、映射别名、memfd、
零复制共享页、权限、只读重挂载、普通卸载和懒卸载。首轮 17 项通过后补齐卸载
检查，只运行受影响的 5 项（其中 1 项为此前已有项），复用其他通过结果。

使用同一静态 ARM64 `mmap-coverage` ELF，在内存/磁盘 × 1/2/4 CPU 完成 6 组
检查，并完成两次独立进程的持久化写入/读取，共 8 组。修改前 runner 的预检查
也通过。原生宿主完成映射检查及持久化写/读三项；原生包装器只调用架构无关的
mappings，不执行 fixture 内的 ARM64 机器码测试。guest 则执行完整 fixture。
本轮没有 QEMU 比较，也没有进行整体性能跑分。

检查中修正了 fixture 本身的一处地址预留：原测试对两页映射外紧邻的一页使用
MAP_FIXED，原生运行可能覆盖无关映射；现在先拥有三页再设置第三页保护，保持
原来的强制 mremap 搬迁验证。修正后原生检查和修改前/后的 guest 检查均通过。

Release CLI/FFI、三个相关 crate 的严格 release Clippy（含测试）、格式与差分
检查通过。未重跑已通过的网络/终端或完整 CPU、GPU、浏览器、Docker/CoreMark。
原始材料在本地 `target/shared-mapping-writers-checks/`，没有制作 IPA、提交或发布。
Linux x86_64 的结果不代替后续 Apple ARM64/iPad 验收。

PTY 主端仍需继续处理：仅共享 devpts 目录索引、把旧 Node 强引用留作缓存会延迟
inotify 删除通知。实际 inode 寿命必须与元数据缓存分开，连同 Unix socket/FIFO、
overlay lower、映射页与任务信号的所有权一起推进，不能依赖下次全体 join 补做关闭。
