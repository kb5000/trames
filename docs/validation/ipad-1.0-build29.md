# iPad 1.0.0 build 29 热修复测试包

[下载ZIP](https://github.com/kb5000/trames/actions/runs/39134234642/artifacts/12138583880)，
解压 `Trames-unsigned.ipa` 用Windows Sideloadly签名安装。Actions需登录，产物保留至
2026-11-01 14:10 UTC。源码 `1a3bcb6a60bd46af11bbdc49113fb34e178d031a`。
未合并main、修改正式tag或创建Release。

build28在用户iPad启动dockerd后发生Rust分配失败/SIGABRT。此次修复
[MMU多余/退休视图和虚拟地址放大](mmu-va-pressure.md)，并修复
[`route -n`地址和掩码字节序](proc-route-endian.md)。guest内存预算仍按用户配置，
本轮4GiB总预算/单进程验证没有改成较小预算。

[Apple CI](https://github.com/kb5000/trames/actions/runs/39134234642)成功：
ARM64、16KiB宿主页，内存库65项、translation4项、proc-route1项；全新Alpine
安装18个原版签名包，默认Docker/containerd/runc/overlay2启动，`docker info`
报告8CPU/4GiB并正常关闭。[启动证据](mmu-va-pressure/apple-results.json)及其
ZIP digest/CRC已核验。未重复未变的CPU、网络、UI和GPU整套验收。

当前aarch64-apple-ios FFI release [optimized]及Xcode Release应用重新编译。
实际下载包核验GitHub ZIP digest、IPA CRC与SHA256；主程序和FileProvider均为
iPhoneOS ARM64，版本1.0.0(29)，App Group一致，未签名；guest musl/Vulkan的
ELF架构及manifest hash保持已验证产物。未含原生CoreMark或调试Wayland样例。

IPA为10,961,206字节，SHA256：

```
738ab119058294936a1f8f5c7a4eb6ad87badbb9221a391df2496dc794c3819b
```

[机器可读包检查和源码闭包](ipad-1.0-build29.json)。Apple开发机验证已通过；
Sideloadly签名后的实际iPad启动和峰值内存仍需用户复测，不将Mac计时当iPad成绩。
