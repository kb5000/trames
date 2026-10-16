# code-server：inode 通知与冷启动连接

2026-10-16，开发宿主使用真实 ARM64/musl Node 24.18.1 和官方 npm
code-server 4.141.0，继续测试 guest 后端、原版 Monaco 工作台和 node-pty。
运行配置仍为 4 vCPU、总提交预算 3 GiB、单地址空间 1 GiB、hybrid musl/pure。
该预算不等于宿主 RSS；iPad/Safari、内存峰值与安装方式仍需单独验收。

## 本轮修复

- [真实 VFS inotify](inotify.md) 已实现，原版 Node 的 fs.watch 独立测试通过。
  运行 Code 后没有再出现之前的 fs.watch ENOSYS。
- 修正开发包导入：npm `socks/build`、`smart-buffer/build` 是正式 JavaScript
  发布目录。原先整体排除 build/，导致 Agent Host 报缺模块。现在仅过滤
  编译对象，恢复原始包文件，没有改写依赖算法。
- [字节补丁脚本](../../scripts/patch-code-server.py) 为原版 Code 的认证、重连、
  ACK、心跳及扩展就绪等待提供有界的开发配置。原始 SHA256、唯一位置和已应用
  状态均预检；默认 10/20/30/60 秒仍保留，测试配置 300 秒。两端保持原有身份
  认证、签名和协议处理，不改变 guest 时钟/CPU/系统调用。
- 手工测试设置可关闭 AI、后台扩展更新和 shell integration。试跑仅写 User
  设置时，120 秒 shell 环境解析配置没有解决服务端超时；脚本现同时写 Machine
  设置，后者尚需单独验收。此设置是 fixture，不自动修改用户环境。
- 浏览器探测等待 Quick Open 的真实文件结果，再检查编辑器区域，执行保存和
  重开；终端通过实际命令创建文件。服务退出后用 VFS export 核对内容。

## 已定位的失败，保留原始证据

[partial-result.json](code-server-iteration/partial-result.json)、
[partial-browser.json](code-server-iteration/partial-browser.json) 和
[partial-server.log](code-server-iteration/partial-server.log) 是中间试跑，
没有通过文件编辑或终端验收：

1. Management 和 ExtensionHost 已建立真实连接，能看到双向 WebSocket 帧，
   但原版 20 秒 ACK/心跳超时会在后端忙于初始化时触发重连。
2. shell 环境解析 10 秒不足；原始 Agent Host 后续因漏导入 socks JS 退出。
3. 只延长认证的 10 秒超时不足以完成整个工作台初始化。

之后一次补齐依赖和 ACK 超时的诊断又确认了独立的 20 秒 keepalive 检查，
因此最终补丁同时处理 ACK 和心跳。开发脚本支持持续运行后端，独立重试浏览器，
避免每次修正测试操作都重新冷启动。

浏览器可请求 identity 编码，用来排查静态资源压缩成本。已有请求耗时包含
后端排队、读取和传输；没有完成独立控制变量对照，不能据此宣称 gzip 是主因。
冷启动仍以分钟计，也不能把页面 DOM 或连接尝试当作完整 code-server 支持。

输入指纹见 [inputs.json](code-server-iteration/inputs.json)；准备和探测命令见
[开发指南](../guides/code-server.md)。

## 最终试跑结果与后续阻塞

[最终浏览器观测](code-server-iteration/final-browser.json)、
[服务日志](code-server-iteration/final-server.log)、
[应用自身日志](code-server-iteration/final-remoteagent.log) 与
[退出后 VFS 导出检查](code-server-iteration/final-result.json) 保留实际结果。

- 完成 Management/ExtensionHost 协议连接与双向帧传输；5 分钟的等待窗口内，
  没有再因旧 20 秒心跳/ACK 限制反复断线。可以生成真实工作台截图。
- Quick Open 对 `/root/project/hello.txt` 等待 300 秒仍为 0 Results；文件没有打开。
  再次连接后尝试 Ctrl+O，没有出现可操作的远端文件对话框，不能计作直接打开成功。
- 终端面板出现，但等待 180 秒仍没有 xterm 输入区域；没有发送 marker 命令。
  工作台命令注册/远端初始化仍在等待；一次独立健康 WebSocket 10 秒内也未响应。
- 停止后经 VFS export，hello.txt 仍是原始内容；终端 marker 不存在。
  **editing_verified=false，terminal_verified=false**。未宣称完整编辑器或终端支持。
- shell 环境解析超时仍在；日志出现 Web Worker Extension Host 启动等待和初始化锁
  警告，旧连接清理后还出现 SIGPIPE。具体卡点尚未定位，不能直接归因于某个 syscall。

为排查 socket 交接，另外用原版 ARM64 Node 24 运行
[node-socket-handoff.cjs](../../tests/fixtures/node-socket-handoff.cjs)：
父进程接受真实 TCP socket，经 fork IPC 交给子进程，子进程发送数据，父进程
验证内容和退出状态；宿主 Node 与 guest 都输出 `node-socket-handoff-ok`。
这个基础路径通过不代表 Code 的完整初始化已通过；已加入原有 Node CI。

下一步需要定位远端初始化/文件 RPC 与子进程启动的等待，并检查服务端 Machine
shell 配置是否生效；之后再分别验收文件与终端。单纯继续延长所有等待时间，
不足以证明问题已解决。开发磁盘可以复用，不必重装现有 iPad 数据。
