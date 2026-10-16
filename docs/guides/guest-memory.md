# Linux 内存预算

设置 → 本地内存可以分别调整 Linux 总预算和单进程预算。默认仍为
总计 1 GiB、单进程 512 MiB；更改在下次打开本地终端时生效。
选项按 iPad 的物理内存筛选，单进程预算不能超过总预算。

命令行对应参数：

```sh
trames-run --storage ./store --memory-budget 3GiB \
  --process-memory-budget 1GiB --guest /bin/ash
```

Rust 嵌入使用 `RuntimeOptions.memory_limits: MemoryLimits`，ABI v3 的
JSON 配置增加可选的 `memory_budget_bytes` 和
`process_memory_budget_bytes`。省略字段保留默认值；预算必须是正的
4 KiB 倍数，单进程不大于总量，总量不超过 guest 48 位地址空间。
会话启动前验证配置，CLI 在打开磁盘前拒绝无效预算。

预算限制已提交的 guest 页面，不预先分配内存。`PROT_NONE` 地址保留
不按整个虚拟范围收费；给页面实际访问权限时收费。`fork` 后每个独立
地址空间保守计费，`CLONE_VM` 线程共享的地址空间只计一次。
`exec` 加载新映像时也受单进程及其他存活地址空间剩余额度限制。
`/proc/meminfo`、`sysinfo` 报告配置的总预算；内存相关 `getrlimit`
报告单进程预算，仍采用既有的已提交页面策略，并非完整 Linux
`RLIMIT_AS` 虚拟地址计费。

该值不是 iPadOS 为 App 保证的可用内存，也不是宿主 RSS 上限。
解释器缓存、页元数据、并行暂存、VFS 和原生库还会占用宿主内存。
请为 iPadOS 和其他应用保留空间；提高预算不会提高系统允许 App
使用的物理内存。
