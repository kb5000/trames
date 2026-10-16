# code-server 开发验证

这是实际 ARM64 Linux 后端的兼容性试跑，尚未作为 iPad 安装功能交付。
HTTP 服务和编辑器欢迎页已启动；远端握手仍超时，文件监听、文件保存、
集成终端和扩展须分别验收。
请使用独立测试磁盘，暂勿把现有 Alpine 环境直接切换到另一发行分支。

## 已采用的输入

- 官方 npm `code-server@4.141.0`，内含 Code 1.141.0，要求 Node 24。
- npm 包 SHA256：`6e5ec40643fc1849838eb7ddf960230fe9fb5de49c88e1618bcde5b175860d65`。
  npm SHA512 integrity：`IvN7k2IADt6KYOifigRzzZSsjxOVZRrB92nB5nyxKIOORo5YMkKP41JFhr1l5kKvikq+eJ+1j4X0fRe/QrtWSQ==`。
- 签名 Alpine v3.24 ARM64 仓库提供 Node 24.18.1-r0、npm 11.12.1-r0
  及其依赖；在基于项目固定 Alpine 3.22.6 的专用磁盘中安装并执行成功。
  开发准备使用 apk 验签，未使用 `--allow-untrusted`。
- Node 24.18.1 headers SHA256：
  `bee7344a7fc6e3252c4822ca916a50cd6ddda9bb39374d6140a8046d9238fded`。
- `node-pty`、`@vscode/spdlog`、`@vscode/native-watchdog`、
  `@vscode/sqlite3` 从包内原始源码构建 ARM64/musl addon。
  `@parcel/watcher`、argon2 使用包内对应 musl ARM64 预编译文件。
  完整扩展集合、Kerberos、Copilot 及所有附加原生模块没有据此获得支持承诺。

官方独立 Linux ARM64 压缩包面向 glibc；本轮使用 npm/musl 路线。
原版 code-server/Code 保留 MIT 许可证及依赖各自的许可证；Trames
集成代码仍为 GPL-3.0-or-later。仓库没有加入这些第三方二进制。

## 准备包目录

在具有 Node 24 和 ARM64/musl 交叉编译器的开发宿主上，下载并验证上述
npm 包、Node headers；解包后分别安装根目录、`lib/vscode` 和
`lib/vscode/extensions` 的生产依赖。安装前禁用脚本，以免运行错误架构
的构建结果：

```sh
npm ci --omit=dev --ignore-scripts --no-audit --no-fund \
  --cpu=arm64 --os=linux --libc=musl
# lib/vscode 使用上游 postinstall 采用的 npm install：
npm install --omit=dev --ignore-scripts --no-audit --no-fund \
  --cpu=arm64 --os=linux --libc=musl
```

4.141.0 的 VS Code shrinkwrap 缺少 `tslib@2.8.1` 条目，`npm ci` 会拒绝；
本轮 `npm install` 补入该依赖。保留原始包和安装后的 shrinkwrap，
不要把这次依赖解析宣称为原始 lockfile 的完全固定重现。

对前述四个 addon 运行 host `node-gyp rebuild --arch=arm64
--nodedir=/path/to/node-v24.18.1`，将 `CC`、`CXX` 指向 ARM64/musl
编译器。开发环境的 clang 14 搭配 GCC 15 headers 时，spdlog 的构建
使用 `CXXFLAGS=-std=c++17`；没有改写 addon 算法源码。核对生成的
`.node` 为 AArch64，依赖 musl/libstdc++，不能误用 glibc 或宿主 addon。

## 可重跑的探测

先准备已装 Node 24 的专用磁盘。若测试亲和性，还需原版 util-linux
的 `/usr/bin/taskset`；Alpine minirootfs 的 BusyBox 未包含 taskset applet。
在第一次导入前应用可选补丁：

```sh
cd /path/to/prepared/code-server/package
patch --fuzz=0 -p1 < /path/to/trames/patches/code-server/4.141.0-handshake.patch
```

该补丁仅使父子进程握手超时可配置，默认仍为上游 10 秒；接受
10,000–600,000 ms 的整数。原版在本轮确实发生 10 秒握手超时。
不改变 guest 时钟、定时器或系统调用语义。使用较长超时：

```sh
CODE_SERVER_HANDSHAKE_TIMEOUT_MS=300000 \
python3 tests/integration/code-server.py \
  --store /path/to/disposable-store \
  --package /path/to/prepared/code-server/package \
  --browser
```

只在第一次导入时传 `--package`；后续省略。脚本默认四核、总预算
3 GiB、单进程 1 GiB、项目 hybrid musl/pure 加速，启动实际 guest
Node，探测 `/healthz`，可选运行宿主 Playwright 浏览器。浏览器需要
单独安装 Playwright 和 Chromium。`--version-only` 仅测版本输出；
`--affinity 0` 是比较用的进程亲和性选项，不是修复多核准备问题的办法。

探测的无认证监听仅绑定宿主 `127.0.0.1`。实际使用保留 code-server
默认密码认证。iPad Safari 需要 Trames 持续运行；切后台会暂停 guest，
应并排显示两者。开发宿主上的浏览器测试不能代替 iPad/Safari 验收。

`--browser` 的 DOM/WebSocket 观测与真正编辑、保存、重开文件、终端
往返测试分开记录；连接尝试不能当作已连通。文件监听需要的 inotify
仍是已知缺口。具体运行结果见开发验证记录。
