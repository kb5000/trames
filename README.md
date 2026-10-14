# Trames

面向 iPadOS 的终端优先开发环境：原生 SSH，加上运行未修改 AArch64 Linux 用户态程序的兼容运行时。使用普通开发者签名，架构不依赖越狱、JIT 权限、私有 API 或硬件虚拟化。

**当前交付：M0–M5 软件实现；完整 iPad 真机验收待完成。** `trames-run` 执行实际静态／动态 AArch64 ELF，支持 32/64 位标量及选定 SIMD 浮点、虚拟进程、管道、信号和一个虚拟 PTY。交互式 BusyBox `ash` 已在内存和磁盘两种后端验证。Linux CLI 默认保存文件到磁盘，新存储目录的文件预算为 16 GiB。IPv4/IPv6 TCP/UDP、真实 musl DNS，以及具有独立 IP 和回环地址的用户态网络栈已验证；Alpine 3.22.6、动态 musl、经过证书和包签名验证的 HTTPS apk update/add，以及安装后 tree 执行已验证。iPad App、原生 SSH/SFTP、Files 授权和 Bridge 已实现并通过软件验证。

0.12.0/build 13 加入[宿主多核执行](docs/architecture/multicore.md)：`--cpus auto|1..32` 控制虚拟 CPU 数，默认自动选择。真实 musl pthread 和 Node.js Worker 可并行执行；ARM 原子、独占访问、acquire/release 和屏障已有同步实现。内核及 NativeCall 初期仍集中处理。[验证和跑分](docs/validation/multicore.md)记录了收益、单核开销和限制；[下载多核 IPA 与安装说明](docs/validation/release-0.12.md)。

后续开发加入[持续 CPU 工作线程](docs/validation/continuous-workers.md)：执行段结束后保留 CPU 状态和内存视图继续运行，系统调用、异常及定时调度点再统一停顿；移除每段的消息往返和启动屏障。对应 [Issue #13](https://github.com/kb5000/trames/issues/13)，已打包为 [0.13.0/build 14 IPA](docs/validation/release-0.13.md)，0.12.0 保留历史记录。

后续 [guest memory／批量原生优化](docs/validation/bulk-native.md)加入共享驻留页直接借用、1 MiB 有界批量路径、ARM64 NEON 扫描比较，以及 musl strcat/strncat；对应 [Issue #15](https://github.com/kb5000/trames/issues/15)。当前开发继续实现 [NativeCall 工作线程并行](docs/architecture/parallel-nativecalls.md)：19 个纯函数、整数/浮点解析和排序步骤可在所属 CPU 上执行并继续 guest 代码，独立原生调用不再要求全局停顿；[验证与跑分](docs/validation/parallel-nativecalls.md)，[Issue #16](https://github.com/kb5000/trames/issues/16)。0.13.0 安装包保持已交付版本。

进一步的[访存优化](docs/architecture/region-memory.md)将同步范围缩到实际触及的 64 字节区段，允许同页独立访问并行；合并脏区段提交、减少映射检查，并为内置后备对象减少单核线程的重复借用。受控 unsafe 保留 ARM 允许的运行结果差异；[验证和原始跑分](docs/validation/region-memory.md)，[Issue #17](https://github.com/kb5000/trames/issues/17)。

[0.14.0/build 15](docs/validation/release-0.14.md)已将这些更新与[稀疏脏页提交优化](docs/validation/checkpoint-cost.md)打包发布到 [GitHub Release](https://github.com/kb5000/trames/releases/tag/v0.14.0)，可直接下载 unsigned IPA 用 Sideloadly 覆盖安装。原生 malloc/free 保持实验性、默认关闭；普通分配器与并行原生函数使用统一的 guest 访存模型。

后续 main 已补齐[sudo 伪终端与宿主时区同步](docs/validation/sudo-timezone.md)：原版 sudo 的默认 `use_pty`、密码认证、提权和嵌套运行通过实际测试；本地 Linux 在启动、回到前台及系统时区变化时接收 iPad 的原始时区规则，保留显式 `TZ` 覆盖。这些更新尚未包含在上述 0.14.0 IPA 中，将随下一次合并版本交付。

main 进一步修复[匿名 ELF 与 fd 执行](docs/validation/fd-exec.md)：`/proc/self/fd/N` 按实际 inode 执行，加入 `execveat`、`AT_EMPTY_PATH` 和 `O_PATH`；关闭原 fd 后仍保留 `/proc/self/exe`。`(deleted)` 只显示一次，仍表示有效的匿名或已取消链接文件。Python `pass_fds` 与 `os.execve(fd)`、23 项原生 Linux 对照通过，随下一次合并版本交付。

## 快速运行

开发分支已加入[实验性 Vulkan 计算桥](docs/architecture/vulkan-compute.md)：原版 Linux loader 通过标准 guest ICD 调用原生 Vulkan；iPad 构建静态链接 MoltenVK／Metal。计算、buffer、映射、pipeline、descriptor、命令缓冲和 fence 的选定接口已打通，默认关闭；[验证](docs/validation/vulkan-compute.md)使用真实软件 Vulkan 驱动，Metal 模拟器与真机验收待完成。图形呈现后续加入，端口转发暂缓；[Issue #21](https://github.com/kb5000/trames/issues/21)。

需要 Rust 1.90.0；交叉构建 BusyBox 和运行差分测试还需要 Python 3、GNU AArch64 GCC/Binutils、Make、curl、bzip2 和 QEMU user-mode。Debian/Ubuntu 可安装：

```sh
sudo apt-get install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu qemu-user build-essential curl bzip2
cargo build --workspace --locked
bash scripts/build-fixtures.sh
cargo run -p trames-cli --bin trames-run -- tests/fixtures/bin/hello
```

实际 guest 输出：

```text
Hello from Trames!
```

运行真实 BusyBox：

```sh
bash scripts/build-busybox.sh
cargo run -p trames-cli --bin trames-run -- target/busybox-build/busybox echo 'Hello BusyBox'
cargo run -p trames-cli --bin trames-run -- --file /data/readme=README.md target/busybox-build/busybox cat /data/readme
cargo run -p trames-cli --bin trames-run -- target/busybox-build/busybox ls /data
```

构建脚本下载并核对固定版本 musl 1.2.5 和 BusyBox 1.37.0 的 SHA-256，用未修改的上游源码交叉编译精简静态配置。已测 applet 为 `true`、`false`、`echo`、`printf`（字符串/整数）、`basename`、`dirname`、`cat`、`wc`、`head`、`tail`、`pwd` 和 `ls`。这些结果不代表所有 BusyBox 配置、参数或发行版二进制都兼容。

`--file /GUEST=HOST` 流式复制单个文件，并自动创建虚拟父目录；目标 guest 文件必须尚不存在。`--export /GUEST=HOST` 在 guest 退出后导出文件，创建新的宿主文件，不覆盖已有目标。guest 的 `/` 和当前目录属于私有虚拟命名空间。导入后的修改需要显式导出才能写回宿主。`--trace-syscalls` 将系统调用诊断写入 stderr。

运行交互式 shell：

```sh
cargo run -p trames-cli --bin trames-run -- --storage target/my-system --install-busybox --pty target/busybox-build/busybox ash -i
```

`--install-busybox` 将该 ELF 安装为 `/bin/busybox` 并建立选定 applet 的硬链接，可在已有存储中重复运行；更新二进制时一并替换其硬链接，已经打开的旧 inode 仍有效。`--exec-file /GUEST=HOST` 导入可执行文件，供虚拟 `execve` 加载。guest 的 fork/exec 均在运行时内部完成。`--pty` 支持 Ctrl-C、Ctrl-Z、Ctrl-D 和作业控制；当前虚拟窗口默认 24×80。

未指定 `--storage` 时，CLI 使用当前宿主目录下的 `.trames/storage`。文件、目录、权限、时间戳、硬链接和符号链接在重启后恢复；存储目录由一个运行实例独占。新目录默认使用 16 GiB 逻辑文件预算，已有目录保留保存的预算；`--storage-budget 16GiB` 可显式设置，`--storage-budget auto` 则取存储卷总容量。可用容量同时受卷剩余空间快照限制，宿主磁盘不足时会返回错误。`--storage-info` 显示启动时的容量。

16 GiB 是配额，不会预分配同等内存或磁盘。普通文件按需读写；导入导出使用 64 KiB 缓冲区。单进程映射内存仍为 64 MiB。`--memory` 可选择临时内存后端，其文件配额仍为 16 MiB。平台 API 已提供容量报告和文件流接口，供后续 iPad Files 授权与文件协调接入；当前尚未完成原生 iPad UI 或设备验证。详见 [存储架构](docs/architecture/storage.md)。

跨两次启动验证持久化：

```sh
target/debug/trames-run --storage target/my-system --install-busybox target/busybox-build/busybox ash -c 'echo saved >/note'
target/debug/trames-run --storage target/my-system --install-busybox --export /note=target/note.txt target/busybox-build/busybox ash -c 'cat /note'
```

默认最多执行 10,000,000 条指令，可使用 `trames-run --max-instructions 1000 ELF [ARG ...]` 调整。运行器传递 guest 退出码；加载失败、CPU 故障或超出预算时返回 125，诊断写入 stderr。guest 也能主动返回 125，调用方应同时查看诊断。

## Alpine 和 apk

~~~sh
bash scripts/build-busybox.sh
bash scripts/prepare-alpine.sh
cargo build --release --workspace --locked
target/release/trames-run --storage target/alpine --rootfs target/alpine-download/alpine-minirootfs-3.22.6-aarch64.tar.gz --guest --max-instructions 100000000 /bin/sh -c 'cat /etc/alpine-release; apk --version'
target/release/trames-run --storage target/alpine --guest --max-instructions 100000000 /bin/sh
python3 tests/integration/m3.py
~~~

`--rootfs` 只接受固定 SHA-256 的上述 Alpine 归档，用于初始化新存储；后续启动仅使用 `--guest`。动态程序的解释器和共享库来自 guest VFS。符号链接持久化，文件私有 mmap 支持 musl 加载库，TLS 使用真实宿主熵源。`--env KEY=VALUE` 显式设置 guest 环境，适用于代理、CA 和库搜索路径。使用公网仓库还需按宿主策略配置 guest DNS／代理及可信 CA；测试套件通过本机 HTTPS 镜像验证原版 TLS 和 Alpine 签名，不会关闭校验。详见 [动态用户态架构](docs/architecture/dynamic-userspace.md) 和 [M3 验证](docs/validation/m3.md)。

## 网络

网络支持实际 IPv4/IPv6 TCP 和 UDP。宿主 socket 始终非阻塞，guest 阻塞调用挂起当前虚拟进程；fork/dup 共享连接，最后一个持有者关闭后释放。每会话最多 64 个 socket，单次复制最多 1 MiB，UDP 数据报保持完整或显式报告截断。

CLI 默认 `--network outbound`：允许出站连接、本机监听及 UDP 客户端的临时端口。`--network off` 关闭网络，`--network loopback` 限定本机，`--network full` 还允许非本机地址和通配监听。loopback 指宿主本机；这不是独立的 guest 网络命名空间。guest 不能绑定 1024 以下端口。模式仍受宿主网络策略和系统权限约束。

使用 `--virtual-network 2` 可进入独立 IP 网络：环境 2 的地址为 `10.0.0.2`／`fd00::2`，网关与 DNS 为 `10.0.0.1`／`fd00::1`，`127.0.0.1`／`::1` 只指向该环境。不同环境拥有独立端口空间，可监听私有 80／53 端口。库的共享 `Fabric` API 可同时连接多个环境；CLI 每次创建一个环境。TCP／UDP 经 smoltcp 数据包栈处理，外网流量再由网关桥接原生 socket，TLS 始终由 guest 库处理。无需 TUN／VPN 权限。虚拟模式在缺少 `/etc/resolv.conf` 时写入虚拟 DNS，保留已有配置。

~~~sh
target/release/trames-run --storage target/alpine --virtual-network 2 --guest --max-instructions 100000000 /bin/sh
~~~

已验证 IPv4／IPv6 大数据报分片重组、TCP 丢包重传、内部 ICMP echo 和 DNS，以及通过网关的真实 Alpine HTTPS apk 安装。当前上限为每个 Fabric 16 个环境、256 个栈句柄（含 backlog）和 64 个原生网关流，每环境 64 个 guest socket。0.2.0 补上 guest raw ICMP、通过原生 ICMP datagram socket 的真实 echo 转发和只读 NETLINK_ROUTE 查询；实际平台权限仍会返回错误。AF_PACKET、组播、DHCP、网络配置修改和完整 Linux 网络控制接口仍未支持。详细行为和资源策略见 [虚拟网络架构](docs/architecture/virtual-network.md) 与 [验证记录](docs/validation/virtual-network.md)。

DNS 由真实 musl 读取 guest 的 `/etc/resolv.conf`；可用 `--file /etc/resolv.conf=HOST_FILE` 显式导入。已验证 A/AAAA 查询和 UDP 截断后的 TCP 回退。HTTP/TLS 由 guest 程序自己的库处理，运行时不继承宿主代理凭据。已验证原版 Alpine HTTPS 和 apk 的证书、主机名及包签名校验；网络/core/distro 已通过 iOS 目标编译检查，Apple 已构建 IPA，用户已在 iPad 上通过 Sideloadly 安装并启动 0.1.0；新功能仍待真机验收。详细设计、行为和缺口见 [网络架构](docs/architecture/network.md)。

## Linux 兼容性更新（0.2.0）

增加运行时生成的 `/proc`、`/sys`、`/dev`：进程、内存、挂载、文件描述符、设备、逻辑接口和数据包统计来自实际 guest 状态。原版 Alpine 的 `ps`、`mount`、`df`、`free`、`top`、`ip a`、`ip route` 和 IPv4／IPv6 `ping` 已纳入执行测试。`mount` 可列出内置挂载，任意文件系统挂载与额外 PTY 分配仍未实现。

guest root、UID/GID、目录权限及持久化所有权修复了安装 Vim 的 88 个目录权限更新错误；0.2.0 曾保留旧格式迁移；按项目所有者要求，0.3.0 只接受当前 v3 格式，提供显式重置。共享内存线程、futex 和 POSIX 定时器支持 musl 的 SIGEV_THREAD，默认配置 Vim 已验证编辑、保存、退出和新会话读取。终端横移修复也合入本次大版本。详细范围与限制见 [Linux 兼容性架构](docs/architecture/linux-compat.md) 和 [验证记录](docs/validation/linux-compat.md)。

## M5 更新（0.3.0 / build 4）

[下载经验证的 unsigned IPA](https://github.com/kb5000/trames/actions/runs/38145322045/artifacts/11704499330)，解压后用 Windows Sideloadly 签名安装。两种 Linux 后端和七项 iPad 模拟器测试通过；[验证记录](docs/validation/m5.md) 包含校验和及验收范围。

首次 Alpine 下载会等待网络授权生效，并对暂时的连接／DNS 错误最多尝试三次；界面在此期间显示正在准备环境。

`/dev/trames-bridge` 和 `trames bridge 'JSON'` 提供版本化异步 RPC。剪贴板、打开网页、设备信息和 Files 复制都要在 iPad 上逐次允许。设置可保存、撤销 Files 目录授权，默认只允许导入；复制限制为单文件 1 GiB，拒绝路径穿越、符号链接和覆盖现有文件。

修复磁盘文件一直占用宿主句柄导致的 `I/O error`：关闭的 guest 文件不再保留宿主文件句柄。新环境默认 16 GiB、131,072 个文件/路径，`/usr` 可写。按所有者选择删除未使用的历史迁移，设置增加“重置 Linux 环境”。真实签名的 `apk add python3`、pycache 修复和实际 Python 执行在低句柄上限下验证。

可选 C++ 后端静态编译，设置开关默认关闭；支持的整数块批量执行，访存/浮点/SIMD/系统调用由 Rust 处理。内置 SHA-256 注册表可显式启用/停用，并记录调用指标。尚无动态原生插件加载。设计、资源边界及验证见 [M5 架构](docs/architecture/m5.md) 和 [M5 验证](docs/validation/m5.md)。

虚拟 DNS 的 `10.0.0.1` 是网关，guest 本机是 `10.0.0.2`。所有者报告的 iPad DNS 错误仍在验证；实际 Apple DNS/HTTPS 已加入测试。手动修改的 resolver 配置不会在重开时改写。

## CPU 参考实现与加速（0.4.0 / build 5）

补齐大量 AArch64 用户态整数 NEON、S/D/FP16 浮点形式、CRC/LSE/RCpc、AES/SHA、
DotProd/RDM/I8MM 和 FlagM；新增六组独立 QEMU 差分共 253,038 个状态快照。
auxv 和 `/proc/cpuinfo` 共用已实现的 CPU 能力，未实现的扩展不公布。
默认 root 会话改用 `/root`，根磁盘标识为 `vda1`，虚拟网络接口为 `eth0`。

正确性验证后优化取指、单页访存与解码分组；Rust 参考后端仍为默认。
[0.4.0 历史能力范围](docs/roadmap/compatibility-0.4.md)和
[验证与前后跑分](docs/validation/reference-0.4.md)记录具体范围。

[下载 0.4.0/build 5 IPA](https://github.com/kb5000/trames/actions/runs/38162066629/artifacts/11711540648)，
解压后用 Sideloadly 签名安装。两种 Linux 后端和七项 iPad 模拟器测试通过。
本轮三组前后对照：参考后端整数吞吐约 1.36×、访存约 2.01×；这是 Linux
CPU 基准，尚不是 iPad 实测速率。

## 验证

以下 Linux 流程需要 AArch64 交叉 GCC/Binutils 和 QEMU 10.0.13。
浮点、SIMD、libm 差分使用实际 ARM64 构建，避免把 x86 开发机的原生 NaN/异常差异
当作 ARM 指令错误；完整包安装和开发工具流程使用开发主机构建。

```sh
cargo fmt --all --check
cargo clippy --workspace --all-targets --locked -- -D warnings
cargo build --workspace --locked
cargo test --workspace --locked
bash scripts/build-qemu-oracle.sh
export PATH="$PWD/target/qemu-oracle/bin:$PATH"
rustup target add aarch64-unknown-linux-musl --toolchain 1.90.0
CARGO_TARGET_AARCH64_UNKNOWN_LINUX_MUSL_LINKER=aarch64-linux-gnu-gcc \
  cargo build -p trames-cli --target aarch64-unknown-linux-musl --release --locked
export TRAMES_RUNNER="$PWD/tests/differential/arm-runner.sh"
python3 tests/differential/run.py
python3 tests/differential/m1.py
python3 tests/differential/floating.py
python3 tests/differential/vector_floating.py
python3 tests/differential/structures.py
python3 tests/differential/pairwise_integer.py
python3 tests/differential/widening_integer.py
python3 tests/differential/vector_copy_shift.py
python3 tests/differential/vector_integer.py
python3 tests/differential/atomics_crc.py
python3 tests/differential/half_floating.py
python3 tests/differential/crypto.py
python3 tests/differential/modern.py
bash scripts/build-busybox.sh
python3 tests/integration/busybox.py
bash scripts/build-m2-fixtures.sh
python3 tests/integration/m2.py
python3 tests/integration/terminal.py
bash scripts/build-storage-fixtures.sh
python3 tests/integration/storage.py
bash scripts/build-network-fixtures.sh
python3 tests/integration/network.py
bash scripts/build-virtual-network-fixtures.sh
bash scripts/build-linux-compat-fixtures.sh
bash scripts/prepare-alpine.sh
cargo build --release --workspace --locked
python3 tests/integration/m3.py
python3 tests/integration/virtual-network.py
python3 tests/integration/linux-compat.py
python3 tests/integration/embedding.py
TRAMES_RUNNER="$PWD/target/release/trames-run" python3 tests/integration/python.py
python3 tests/differential/frint_rcpc.py
python3 tests/differential/coverage_extensions.py
bash scripts/build-cpu-fixtures.sh
python3 tests/integration/cpu-signals.py
cargo build -p trames-cpu --example instruction_probe --release --locked
python3 scripts/audit-instructions.py
rustup target add aarch64-apple-ios --toolchain 1.90.0
cargo check -p trames-core -p trames-distro --target aarch64-apple-ios --locked
TRAMES_TEST_STORAGE=disk python3 tests/integration/m2.py
TRAMES_TEST_STORAGE=disk python3 tests/integration/terminal.py
```

网络测试的本机 DNS peer 需要绑定宿主 53 端口的权限；没有权限时，可参照 [网络验证](docs/validation/network.md) 使用 sudo 运行测试。它不改变宿主 DNS 配置。iOS 目标检查只做编译，不链接 Apple SDK。

差分脚本独立运行同一个静态 ELF，比较 Trames 与 QEMU 的 stdout、stderr 和退出码。六组固定程序检查初始栈、BSS、跨页读写、分支、系统调用错误和二进制输出；64 个确定性生成程序比较八个寄存器及 NZCV 的 72 字节内存快照。另有执行预算、非法指令、代码页写保护检查，以及改名并改变 ELF 内容的验证。

M1 再加入 64 个生成程序，对照位域、扩展寄存器运算、乘除、进位、条件选择及 NZCV；固定程序验证成对访存、符号扩展、SIMD 数据传送、exclusive monitor、匿名内存和文件创建/读写。M2 加入 6,860 个浮点用例、159 个结构访存变体、22 个 shell 场景和两个静态 C 程序。存储阶段增加真实文件 ABI、文件管理 applet、48 个整数 SIMD 快照，以及磁盘后端的完整 M2 重跑。网络基础再加入内存/磁盘两种后端的 36 次网络对照、16 项能力/缓冲区预检/会话清理结果，以及 iOS 目标编译检查。M3 再加入 36 次动态程序／TLS／包管理对照和 72 个 widening SIMD 快照。虚拟网络再加入 27 次对照和协议／隔离测试。上述早期阶段共有 554 次 QEMU 程序/场景比较；0.2.0 再增加 musl 线程/定时器比较、CPU 指令快照、完整运行时端点与默认 Vim 编辑验证。0.2.0 时 Rust 单元/集成测试共 115 项，终端交互仍在两种后端各验证 9 项结果；另验证配额、损坏元数据、提交失败回滚、流式复制、宿主范围及强制结束后的恢复。详见 [存储验证](docs/validation/storage.md) 和 [网络验证](docs/validation/network.md)。

可通过 `AARCH64_AS`、`AARCH64_LD`、`QEMU_AARCH64` 覆盖工具路径，BusyBox 构建另支持 `AARCH64_CC`、`AARCH64_AR`、`AARCH64_RANLIB`；`--runner` 选择运行器，`--seeds` 调整生成程序数量。QEMU 是独立参考，测试通过只证明已测行为。

## M6 开发工具与系统调用（0.5.0 / build 6）

[下载经验证的 0.5.0/build 6 unsigned IPA](https://github.com/kb5000/trames/actions/runs/38170280417/artifacts/11713803470)，
解压后用 Windows Sideloadly 签名更新。两种 Linux 后端的完整 CI 与七项 iPad 模拟器测试通过；
包内校验和、设备平台和应用标识已核验，具体记录见下方验证文档。

补齐 epoll/eventfd/timerfd、Unix socketpair、POSIX 文件锁、命名 FIFO 和管道非阻塞设置。
修复 `adduser` 的 passwd 锁、`su` 后执行程序时身份被重置，以及 `htop` 所需的 `/proc/stat` btime。
Git 的目录创建也按 Linux 的尾部斜杠语义修正。

验证流程覆盖真实 Alpine Git 提交/合并/克隆、GCC/Clang 编译链接执行、Make 并行构建、
CMake/CTest，以及 Python asyncio/线程/SQLite/venv。Clang 包的命令为 `clang-19`。
每进程映射额度为 512 MiB，所有地址空间合计 1 GiB，以容纳 LLVM 和编译子进程。
[能力与剩余缺口](docs/roadmap/compatibility-m6.md) · [验证记录](docs/validation/m6.md)。

## 非 JIT 提速（0.7.0 / build 8）

[下载经验证的 0.7.0/build 8 unsigned IPA](https://github.com/kb5000/trames/actions/runs/38205290951/artifacts/11727840985)，
解压后用相同 Apple ID／有效 bundle ID 的 Windows Sideloadly 覆盖安装，保留当前 Linux 数据。
默认使用 Rust 缓存解释器，无需启用 JIT；若此前打开了试验性 C++ 开关，关闭它后重新打开本地终端。
三个 Linux 后端的完整 CI 与七项 iPad 模拟器测试通过；安装包已与实际设备构建逐文件核验。

相对 0.6.0 的缓存后端，在相同磁盘数据上交错测量三组，默认 Vim 启动从 1.94 秒降至 1.78 秒，
GCC 编译链接运行约快 3%；Python 标准库／SQLite／JSON／hash 工作负载无明显变化。
这些是 Linux 主机的中位数，iPad 速度仍需真机测量。Vim 保留正常配置与插件，未使用最小启动参数。
[上一轮 0.5→0.6 的较大提速](docs/validation/nonjit-0.6.md)保留为独立批次。

本轮加入代码页的两路缓存、逐页失效校验和比较／跳转合并，并补齐 FRINT32/64 浮点取整、
LDAPUR/STLUR 等带偏移的 acquire/release 访存。单步参考实现继续用于对照；14 组 QEMU 差分、
自修改代码、共享内存、权限变化、跨页故障及精确指令预算验证通过。
显式尾调用已做 Clang/iOS arm64 原型；合成测试较慢，因此默认后端继续使用 Rust 循环分派。
[设计](docs/architecture/nonjit.md) · [尾调用研究](docs/architecture/tail-dispatch.md) · [性能与发布验证](docs/validation/nonjit-0.7.md)。

## 指令扩展与原生运算（0.8.0 / build 9）

[下载已核验的 0.8.0/build 9 unsigned IPA](https://github.com/kb5000/trames/actions/runs/38236217045/artifacts/11741258008)，
解压后使用相同 Apple ID／有效 bundle ID 的 Windows Sideloadly 覆盖安装，保留 Linux 数据。

0.8.0/build 9 开始，源码已统一为一个 Rust 缓存解释器，移除 C++ 与尾调用原型。
浮点、SIMD 和密码运算优先执行静态 ARM64 原生指令，仍无需 JIT。
本轮增加 CSSC、BF16、复数与扩展乘加、SHA512/SHA3/SM3/SM4、MOPS 和更多访存指令；
PAC/BTI/MTE 按功能兼容策略处理，实际跳转、访存、写回和清零仍执行。
CPU 故障可交给 Linux 信号处理器恢复。完整 Linux CI 与七项 iPad 模拟器测试通过；
IPA 已核验 ARM64 设备目标、版本、执行权限及全部 36 个文件。真机速度仍需实际测量。
[指令范围与验证](docs/validation/instruction-coverage.md) · [当前编码清单](docs/validation/instruction-coverage-current.csv) ·
[当前 CPU/Linux 剩余缺口](docs/roadmap/compatibility-current.md) ·
[系统调用清单](docs/validation/compatibility-current-syscalls.csv)。

## 文件接口、共享映射与系统日志（0.9.0 / build 10）

补充 statx、preadv/pwritev、close_range（含 CLOEXEC/UNSHARE）及 syslog。
`dmesg` 可查看未实现系统调用的名称/编号、参数和 PC，以及 CPU 故障的指令编码。
`/dev/kmsg` 提供共享、有界的会话日志与独立读取游标；日志不写入磁盘，有限流。
进一步补齐共享文件/匿名 mmap、EOF/truncate SIGBUS、msync/mremap/mincore/madvise、
memfd 与 classic seals、文件复制/批量 I/O，以及单 CPU 的调度/亲和性接口。
目前已实现子集增至 161 个，未实现降至 155 个，另有 2 个废弃条目。
[下载已核验的 0.9.0/build 10 unsigned IPA](https://github.com/kb5000/trames/actions/runs/38251377309/artifacts/11749317346)。完整 Linux / Apple CI 与七项 iPad 模拟器测试通过。
解压后用 Windows Sideloadly 签名。磁盘格式和 bundle ID 保持稳定，
继续使用相同 Sideloadly Apple ID／有效 bundle ID 覆盖安装。
[发布记录](docs/validation/release-0.9.md)。
[系统日志](docs/validation/compatibility-log.md) · [共享映射与 ABI 验证](docs/validation/mmap-coverage.md)。

后续开发：[NativeCall v1 纯函数加速](docs/architecture/accelerator-v1.md) 已打通 guest ELF 桩库和应用内置原生后端，
覆盖 `memcpy`、`memmove`、`memset`、`memcmp`、`strlen`，默认关闭；CLI 用 `--accelerator pure` 显式启用。
直接复用固定版本的 musl / Arm 原始实现，保留 GPL 与上游许可记录。
[完整 Accelerator RFC](docs/architecture/accelerator.md) 的有状态 libc、GPU/video/ML 仍在后续计划中。
当前 0.9.0 安装包早于这批开发代码，后续合并发布再提供新 IPA。

## 结构与范围

| crate | 当前职责 |
| --- | --- |
| `trames-memory` | 4 KiB 稀疏页面、fork 写时复制、内存上限、R/W/X 及映射保护 |
| `trames-elf` | ELF64/AArch64 ET_EXEC/ET_DYN、PT_INTERP、PT_LOAD、BSS 和加载偏移校验 |
| `trames-cpu` | 整数/访存/原子操作、标量与 SIMD 浮点、FPCR/FPSR、TLS/NZCV |
| `trames-compute` | 数值输入的静态 ARM64 原生运算、可选特性检测、宿主浮点状态恢复 |
| `trames-linux` | 有状态文件描述符、读写/目录/stat、私有/共享文件与匿名 mmap/brk、flock/getrandom、负 errno、字节流 |
| `trames-net` | 有界非阻塞 TCP/UDP、网络能力、宿主错误翻译和就绪等待 |
| `trames-vfs` | 共享虚拟命名空间、磁盘/内存文件、配额与容量、打开描述与管道 |
| `trames-core` | ELF/初始栈、进程调度、exec/wait、信号帧、虚拟 PTY |
| `trames-distro` | 固定 SHA-256 Alpine rootfs、受限归档解析与 VFS 导入 |
| `trames-cli` | `trames-run`，宿主字节流输出和退出码 |

尚不支持重叠 PT_LOAD 页面、完整 AArch64、SVE、备用信号栈、多个 PTY、非零 VTIME、完整需求分页/回收与所有映射扩展。线程、vfork 和 futex 已有选定实现；其余 Linux 缺口见[当前清单](docs/roadmap/compatibility-current.md)。未实现的指令明确报告故障，未知系统调用返回 `-ENOSYS`。安全限制及其兼容性影响见 [security](docs/security/README.md)。

[工程章程](docs/charter.md) · [存储架构](docs/architecture/storage.md) · [路线图](docs/roadmap/README.md) · [存储验证](docs/validation/storage.md) · [BusyBox 构建](docs/testing/busybox.md)

## 许可证

Copyright (C) 2026 Trames contributors. 本项目使用 **GNU GPL version 3 or later**（`GPL-3.0-or-later`），完整文本见 [LICENSE](LICENSE)。引入 GPL、LGPL、AGPL 或其他依赖时需分别核对许可证和组合方式；网络使用 socket2/nix 的安全接口，依赖许可记录见 [dependencies](docs/security/dependencies.md)。GNU Binutils、Rust 和 QEMU 为开发工具，不随运行器链接发布。

The iPad frontend is in [apps/ios](apps/ios/README.md), with build scripts and a
[device acceptance record](docs/validation/m4.md). M4 is not accepted until an
ordinary developer-signed physical iPad run is validated.

0.11.0/build 12 汇总完整标准路径 musl 的 17 个纯函数、整数/浮点解析、
可嵌套比较器的排序调用、直接 guest 页访问与解释器访存优化。新增真实 Node.js/npm
兼容测试，并补齐地址预留、进程参数排布、信号与标准流行为；仍使用单一缓存解释器，无宿主 JIT。
iPad 启动时原子更新随包的标准 libc，保留 Linux 用户数据。
[0.11.0 安装包与发布记录](docs/validation/release-0.11.md) ·
[Node.js 和内存验证](docs/validation/nodejs-memory.md) · [完整 musl 架构](docs/architecture/musl-hybrid.md)。

0.12.0/build 13 将[宿主多核执行](docs/architecture/multicore.md)打包为一次更新，
默认按宿主可用逻辑 CPU 数运行，补齐并行原子/屏障、共享 futex 与绑核公平性。
[0.12.0 安装包、源码与 CI 核验](docs/validation/release-0.12.md)。
