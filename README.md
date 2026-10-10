# Trames

面向 iPadOS 的终端优先开发环境：原生 SSH，加上运行未修改 AArch64 Linux 用户态程序的兼容运行时。使用普通开发者签名，架构不依赖越狱、JIT 权限、私有 API 或硬件虚拟化。

**当前交付：M0–M2 的 Linux 开发基础。** `trames-run` 执行实际静态 AArch64 ELF，支持 32/64 位标量及主要 SIMD 浮点、虚拟进程、管道、信号和一个虚拟 PTY。已验证交互式 BusyBox `ash` 的命令执行、重定向、管道及 `jobs/bg/fg`。iPad App、SSH、动态链接、Alpine 和包管理属于后续里程碑。

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
cargo run -p trames-cli --bin trames-run -- --file /data/readme=README.md target/busybox-build/busybox ls /data
```

构建脚本下载并核对固定版本 musl 1.2.5 和 BusyBox 1.37.0 的 SHA-256，用未修改的上游源码交叉编译精简静态配置。已测 applet 为 `true`、`false`、`echo`、`printf`（字符串/整数）、`basename`、`dirname`、`cat`、`wc`、`head`、`tail`、`pwd` 和 `ls`。这些结果不代表所有 BusyBox 配置、参数或发行版二进制都兼容。

`--file /GUEST=HOST` 显式导入单个文件的字节，并自动创建虚拟父目录。guest 只能访问导入文件、其创建的虚拟文件和 `/dev/null`；虚拟写入保存在内存中。guest 的 `/` 和当前目录不对应宿主目录。可重复使用 `--file`，或使用标准输入传递数据。`--trace-syscalls` 将系统调用诊断写入 stderr。

运行交互式 shell：

```sh
cargo run -p trames-cli --bin trames-run -- --install-busybox --pty target/busybox-build/busybox ash -i
```

`--install-busybox` 将该 ELF 作为 `/bin/busybox` 导入，并建立选定 applet 的硬链接。`--exec-file /GUEST=HOST` 导入可执行文件，供虚拟 `execve` 加载。guest 的 fork/exec 均在运行时内部完成。`--pty` 支持 Ctrl-C、Ctrl-Z、Ctrl-D 和作业控制；当前虚拟窗口默认 24×80。

文件仍保存在内存中；文件内容配额维持 16 MiB，单进程映射内存维持 64 MiB。这轮没有提高容量。磁盘存储、先 16 GiB 的文件预算、后续按 iPad 存储容量配置及 Files 互通一起留到后续实现。

默认最多执行 10,000,000 条指令，可使用 `trames-run --max-instructions 1000 ELF [ARG ...]` 调整。运行器传递 guest 退出码；加载失败、CPU 故障或超出预算时返回 125，诊断写入 stderr。guest 也能主动返回 125，调用方应同时查看诊断。

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
bash scripts/build-busybox.sh
python3 tests/integration/busybox.py
bash scripts/build-m2-fixtures.sh
python3 tests/integration/m2.py
python3 tests/integration/terminal.py
```

差分脚本独立运行同一个静态 ELF，比较 Trames 与 QEMU 的 stdout、stderr 和退出码。六组固定程序检查初始栈、BSS、跨页读写、分支、系统调用错误和二进制输出；64 个确定性生成程序比较八个寄存器及 NZCV 的 72 字节内存快照。另有执行预算、非法指令、代码页写保护检查，以及改名并改变 ELF 内容的验证。

M1 再加入 64 个生成程序，对照位域、扩展寄存器运算、乘除、进位、条件选择及 NZCV；固定程序验证成对访存、符号扩展、SIMD 数据传送、exclusive monitor、匿名内存和文件创建/读写。BusyBox 集成比较 stdout/stderr/退出码，另检查未导入的宿主文件无法被 guest 访问。M2 加入 6,860 个浮点用例、159 个结构访存变体、22 个 shell 场景和两个静态 C 程序。当前共 427 次 QEMU 程序/场景比较、9 项终端交互结果对照、64 个单元测试和 4 项独立故障/宿主范围检查。

可通过 `AARCH64_AS`、`AARCH64_LD`、`QEMU_AARCH64` 覆盖工具路径，BusyBox 构建另支持 `AARCH64_CC`、`AARCH64_AR`、`AARCH64_RANLIB`；`--runner` 选择运行器，`--seeds` 调整生成程序数量。QEMU 是独立参考，测试通过只证明已测行为。

## 结构与范围

| crate | 当前职责 |
| --- | --- |
| `trames-memory` | 4 KiB 稀疏页面、fork 写时复制、内存上限、R/W/X 及映射保护 |
| `trames-elf` | 静态 ELF64/AArch64 ET_EXEC 校验，PT_LOAD 加载及 BSS 清零 |
| `trames-cpu` | 整数/访存/原子操作、标量与 SIMD 浮点、FPCR/FPSR、TLS/NZCV |
| `trames-linux` | 有状态文件描述符、读写/目录/stat、匿名 mmap/brk、负 errno、字节流 |
| `trames-vfs` | 共享内存文件系统、打开描述与管道，路径/文件/目录、`/dev/null` |
| `trames-core` | ELF/初始栈、进程调度、exec/wait、信号帧、虚拟 PTY |
| `trames-cli` | `trames-run`，宿主字节流输出和退出码 |

尚不支持 ET_DYN、PT_INTERP、PT_DYNAMIC、重叠 PT_LOAD 页面、完整 AArch64/SIMD、FP16/SVE、线程/共享地址空间/vfork、阻塞 futex、备用信号栈、多个 PTY、非零 VTIME、符号链接或文件映射 mmap。未实现的指令明确报告故障，未知系统调用返回 `-ENOSYS`。安全限制及其兼容性影响见 [security](docs/security/README.md)。

[工程章程](docs/charter.md) · [M2 架构](docs/architecture/m2.md) · [路线图](docs/roadmap/README.md) · [M2 验证记录](docs/validation/m2.md) · [BusyBox 构建](docs/testing/busybox.md)

## 许可证

Copyright (C) 2026 Trames contributors. 本项目使用 **GNU GPL version 3 or later**（`GPL-3.0-or-later`），完整文本见 [LICENSE](LICENSE)。引入 GPL、LGPL、AGPL 或其他依赖时需分别核对许可证和组合方式；当前 Rust 工作区无第三方 crate 依赖。GNU Binutils、Rust 和 QEMU 为开发工具，不随运行器链接发布。
