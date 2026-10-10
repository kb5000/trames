# Trames

面向 iPadOS 的终端优先开发环境：原生 SSH，加上运行未修改 AArch64 Linux 用户态程序的兼容运行时。使用普通开发者签名，架构不依赖越狱、JIT 权限、私有 API 或硬件虚拟化。

**当前交付：M0–M3、磁盘持久化和用户态虚拟网络。** `trames-run` 执行实际静态／动态 AArch64 ELF，支持 32/64 位标量及选定 SIMD 浮点、虚拟进程、管道、信号和一个虚拟 PTY。交互式 BusyBox `ash` 已在内存和磁盘两种后端验证。Linux CLI 默认保存文件到磁盘，新存储目录的文件预算为 16 GiB。IPv4/IPv6 TCP/UDP、真实 musl DNS，以及具有独立 IP 和回环地址的用户态网络栈已验证；Alpine 3.22.6、动态 musl、经过证书和包签名验证的 HTTPS apk update/add，以及安装后 tree 执行已验证。iPad App、原生 Files 授权界面和 SSH 属于后续里程碑。

## 快速运行

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

已验证 IPv4／IPv6 大数据报分片重组、TCP 丢包重传、内部 ICMP echo 和 DNS，以及通过网关的真实 Alpine HTTPS apk 安装。当前上限为每个 Fabric 16 个环境、256 个栈句柄（含 backlog）和 64 个原生网关流，每环境 64 个 guest socket。外部 ICMP、raw／packet socket、组播、DHCP、netlink 和完整 Linux 网络控制接口仍未支持。详细行为和资源策略见 [虚拟网络架构](docs/architecture/virtual-network.md) 与 [验证记录](docs/validation/virtual-network.md)。

DNS 由真实 musl 读取 guest 的 `/etc/resolv.conf`；可用 `--file /etc/resolv.conf=HOST_FILE` 显式导入。已验证 A/AAAA 查询和 UDP 截断后的 TCP 回退。HTTP/TLS 由 guest 程序自己的库处理，运行时不继承宿主代理凭据。已验证原版 Alpine HTTPS 和 apk 的证书、主机名及包签名校验；网络/core/distro 已通过 iOS 目标编译检查，尚无签名 App 或真机验证。详细设计、行为和缺口见 [网络架构](docs/architecture/network.md)。

## 验证

```sh
cargo fmt --all --check
cargo clippy --workspace --all-targets --locked -- -D warnings
cargo build --workspace --locked
cargo test --workspace --locked
python3 tests/differential/run.py
python3 tests/differential/m1.py
python3 tests/differential/floating.py
python3 tests/differential/vector_floating.py
python3 tests/differential/structures.py
python3 tests/differential/pairwise_integer.py
python3 tests/differential/widening_integer.py
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
bash scripts/prepare-alpine.sh
cargo build --release --workspace --locked
python3 tests/integration/m3.py
python3 tests/integration/virtual-network.py
rustup target add aarch64-apple-ios --toolchain 1.90.0
cargo check -p trames-core -p trames-distro --target aarch64-apple-ios --locked
TRAMES_TEST_STORAGE=disk python3 tests/integration/m2.py
TRAMES_TEST_STORAGE=disk python3 tests/integration/terminal.py
```

网络测试的本机 DNS peer 需要绑定宿主 53 端口的权限；没有权限时，可参照 [网络验证](docs/validation/network.md) 使用 sudo 运行测试。它不改变宿主 DNS 配置。iOS 目标检查只做编译，不链接 Apple SDK。

差分脚本独立运行同一个静态 ELF，比较 Trames 与 QEMU 的 stdout、stderr 和退出码。六组固定程序检查初始栈、BSS、跨页读写、分支、系统调用错误和二进制输出；64 个确定性生成程序比较八个寄存器及 NZCV 的 72 字节内存快照。另有执行预算、非法指令、代码页写保护检查，以及改名并改变 ELF 内容的验证。

M1 再加入 64 个生成程序，对照位域、扩展寄存器运算、乘除、进位、条件选择及 NZCV；固定程序验证成对访存、符号扩展、SIMD 数据传送、exclusive monitor、匿名内存和文件创建/读写。M2 加入 6,860 个浮点用例、159 个结构访存变体、22 个 shell 场景和两个静态 C 程序。存储阶段增加真实文件 ABI、文件管理 applet、48 个整数 SIMD 快照，以及磁盘后端的完整 M2 重跑。网络基础再加入内存/磁盘两种后端的 36 次网络对照、16 项能力/缓冲区预检/会话清理结果，以及 iOS 目标编译检查。M3 再加入 36 次动态程序／TLS／包管理对照和 72 个 widening SIMD 快照。虚拟网络再加入 27 次对照和协议／隔离测试。当前共 554 次 QEMU 程序/场景比较、两种后端各 9 项终端结果、91 个单元测试及 11 个 Rust 网络集成测试；另验证配额、损坏元数据、提交失败回滚、流式复制、宿主范围及强制结束后的恢复。详见 [存储验证](docs/validation/storage.md) 和 [网络验证](docs/validation/network.md)。

可通过 `AARCH64_AS`、`AARCH64_LD`、`QEMU_AARCH64` 覆盖工具路径，BusyBox 构建另支持 `AARCH64_CC`、`AARCH64_AR`、`AARCH64_RANLIB`；`--runner` 选择运行器，`--seeds` 调整生成程序数量。QEMU 是独立参考，测试通过只证明已测行为。

## 结构与范围

| crate | 当前职责 |
| --- | --- |
| `trames-memory` | 4 KiB 稀疏页面、fork 写时复制、内存上限、R/W/X 及映射保护 |
| `trames-elf` | ELF64/AArch64 ET_EXEC/ET_DYN、PT_INTERP、PT_LOAD、BSS 和加载偏移校验 |
| `trames-cpu` | 整数/访存/原子操作、标量与 SIMD 浮点、FPCR/FPSR、TLS/NZCV |
| `trames-linux` | 有状态文件描述符、读写/目录/stat、私有文件／匿名 mmap/brk、flock/getrandom、负 errno、字节流 |
| `trames-net` | 有界非阻塞 TCP/UDP、网络能力、宿主错误翻译和就绪等待 |
| `trames-vfs` | 共享虚拟命名空间、磁盘/内存文件、配额与容量、打开描述与管道 |
| `trames-core` | ELF/初始栈、进程调度、exec/wait、信号帧、虚拟 PTY |
| `trames-distro` | 固定 SHA-256 Alpine rootfs、受限归档解析与 VFS 导入 |
| `trames-cli` | `trames-run`，宿主字节流输出和退出码 |

尚不支持重叠 PT_LOAD 页面、完整 AArch64/SIMD、FP16/SVE、线程/共享地址空间/vfork、阻塞 futex、备用信号栈、多个 PTY、非零 VTIME、共享文件 mmap 或映射超过 EOF 的 SIGBUS 语义。未实现的指令明确报告故障，未知系统调用返回 `-ENOSYS`。安全限制及其兼容性影响见 [security](docs/security/README.md)。

[工程章程](docs/charter.md) · [存储架构](docs/architecture/storage.md) · [路线图](docs/roadmap/README.md) · [存储验证](docs/validation/storage.md) · [BusyBox 构建](docs/testing/busybox.md)

## 许可证

Copyright (C) 2026 Trames contributors. 本项目使用 **GNU GPL version 3 or later**（`GPL-3.0-or-later`），完整文本见 [LICENSE](LICENSE)。引入 GPL、LGPL、AGPL 或其他依赖时需分别核对许可证和组合方式；网络使用 socket2/nix 的安全接口，依赖许可记录见 [dependencies](docs/security/dependencies.md)。GNU Binutils、Rust 和 QEMU 为开发工具，不随运行器链接发布。
