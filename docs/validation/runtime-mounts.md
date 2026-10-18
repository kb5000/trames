# iPad 启动时自动挂载运行目录

build30在安装/加载rootfs和musl之后、DNS默认配置/Wayland监听/终端init之前，
为当前guest内核会话准备 `/run` tmpfs（0755）和 `/dev/shm` tmpfs（1777、64MiB）。
额度按需使用，空挂载不预分配64MiB。多个本地终端共享同一个mount namespace；
再次准备跳过已有挂载，保留活跃Wayland socket和PID文件。冷启动获得新的tmpfs，
不会把旧会话PID和共享内存文件恢复成当前服务状态。底层磁盘用户文件不做迁移。

只修改FFI会话初始化；独立CLI保持由init或调用者准备。build29的MMU/route/
Docker及未变CPU/UI/GPU/guest产物继续保留各自通过证据，不重复完整验收。

两个Linux定向回归和FFI严格Clippy通过：检查tmpfs magic、目录权限、64MiB额度
和空闲块；Wayland建立后重复调用不叠加挂载或遮住socket；保留活文件/共享终端
namespace；磁盘冷重开没有上一会话的临时文件。Apple同项检查和build30实际
ARM64 iOS Release IPA构建随后执行。[本地日志](runtime-mounts/tests.log)。
