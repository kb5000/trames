# Trames

面向 iPadOS 的终端优先开发环境：原生 SSH，加上运行未修改 AArch64 Linux 用户态程序的兼容运行时。使用普通开发者签名，架构不依赖越狱、JIT 权限、私有 API 或硬件虚拟化。

**当前交付：M0 的 Linux 开发基础。** `trames-run` 会加载实际 ELF 文件，由 Rust 解释器执行 AArch64 指令，通过 Linux ABI 输出字节并退出。iPad App、SSH、BusyBox、动态链接、Alpine 和包管理属于后续里程碑。

## 快速运行

需要 Rust 1.90.0；构建测试程序和运行差分测试还需要 Python 3、GNU AArch64 Binutils 和 QEMU user-mode。Debian/Ubuntu 可安装：

```sh
sudo apt-get install binutils-aarch64-linux-gnu qemu-user
cargo build --workspace --locked
bash scripts/build-fixtures.sh
cargo run -p trames-cli --bin trames-run -- tests/fixtures/bin/hello
```

实际 guest 输出：

```text
Hello from Trames!
```

默认最多执行 10,000,000 条指令，可使用 `trames-run --max-instructions 1000 ELF [ARG ...]` 调整。运行器传递 guest 退出码；加载失败、CPU 故障或超出预算时返回 125，诊断写入 stderr。guest 也能主动返回 125，调用方应同时查看诊断。

## 验证

```sh
cargo fmt --all --check
cargo clippy --workspace --all-targets --locked -- -D warnings
cargo build --workspace --locked
cargo test --workspace --locked
python3 tests/differential/run.py
```

差分脚本独立运行同一个静态 ELF，比较 Trames 与 QEMU 的 stdout、stderr 和退出码。六组固定程序检查初始栈、BSS、跨页读写、分支、系统调用错误和二进制输出；64 个确定性生成程序比较八个寄存器及 NZCV 的 72 字节内存快照。另有执行预算、非法指令、代码页写保护检查，以及改名并改变 ELF 内容的验证。

可通过 `AARCH64_AS`、`AARCH64_LD`、`QEMU_AARCH64` 覆盖工具路径；`--runner` 选择 release 运行器，`--seeds` 调整生成程序数量。QEMU 是独立参考，测试通过只证明已测指令子集的行为。

## 结构与范围

| crate | 当前职责 |
| --- | --- |
| `trames-memory` | 4 KiB 稀疏页面，内存上限，R/W/X 保护，检查 guest 地址 |
| `trames-elf` | 静态 ELF64/AArch64 ET_EXEC 校验，PT_LOAD 加载及 BSS 清零 |
| `trames-cpu` | 架构状态，整数指令子集，条件标志，限额执行切片，SVC/故障事件 |
| `trames-linux` | AArch64 `write` / `exit` / `exit_group`，负 errno，输出抽象 |
| `trames-core` | ELF、16 字节对齐的 argv/envp/auxv 初始栈、运行循环 |
| `trames-cli` | `trames-run`，宿主字节流输出和退出码 |

尚不支持 ET_DYN、PT_INTERP、PT_DYNAMIC、重叠 PT_LOAD 页面及完整 AArch64 指令集。未实现的指令明确报告故障，未知系统调用返回 `-ENOSYS`。安全限制及其兼容性影响见 [security](docs/security/README.md)。

[工程章程](docs/charter.md) · [架构](docs/architecture/m0.md) · [路线图](docs/roadmap/README.md) · [验证记录](docs/validation/m0.md)

## 许可证

Copyright (C) 2026 Trames contributors. 本项目使用 **GNU GPL version 3 or later**（`GPL-3.0-or-later`），完整文本见 [LICENSE](LICENSE)。引入 GPL、LGPL、AGPL 或其他依赖时需分别核对许可证和组合方式；当前 Rust 工作区无第三方 crate 依赖。GNU Binutils、Rust 和 QEMU 为开发工具，不随运行器链接发布。
