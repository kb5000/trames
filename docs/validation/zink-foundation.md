# Zink 查询与模板基础（2026-10-19）

性能PR #31已合入main121cdc7，按用户要求继续单线开发。
本轮使用原版Alpine签名Mesa25.1.9-r0和vulkan-loader1.4.313.0-r0，未修改Mesa。
上游源码`https://archive.mesa3d.org/mesa-25.1.9.tar.xz`，SHA-256
`412df33a1bb3c785ed698555a3972118a37c458e7accf6ae53f4bb87b3db454a`。

原版Zink/EGL首次在本地CPU physical device上报告`failed to choose pdev`。
用其标准CPU参考开关后，继续因Vulkan硬依赖不足而无法初始化EGL。
补查询后还发现现代Mesa传入的较高实例API请求需要实例1.1语义。
实例查询升级后physical-device API仍保留1.0受限profile；没有伪装全部设备1.1能力。

最终本地结果：

| 项目 | 结果 |
| --- | --- |
| 原版loader实例API1.3请求→1.1查询实现 | 通过 |
| legacy/2/KHR查询、group、容量canary、未知pNext与Features2设备创建 | 通过 |
| memory requirements2、无外部handle能力、扩展专用设备未误开WSI | 通过 |
| template buffer偏移载荷→GPU计算→4096整数，多轮/二进制信号量 | 与原生Vulkan逐字节输出相同 |
| template图像/采样器数组、stride与uniform→三轮纹理绘制 | 独立像素方程通过，导出图与原生Vulkan逐字节相同 |
| 同一新版ICD的原有compute/texture路径 | 通过 |
| App旧manifest原子刷新与重复安装 | 两项Rust测试通过；相关严格Clippy通过 |
| 真实FFI冷进程首次/二次/更新/关GPU/再开GPU启动 | 五种模式通过，保留用户数据 |
| 原版Mesa Zink `eglInitialize` | **失败，未进入GL渲染** |

`zink-foundation.py`返回基础测试状态，但始终单列`rendering_passed:false`，
实际Mesa probe exit1和stdout/stderr保留；加`--require-render`会使未通过GL初始化成为失败门禁。
实测缺少maintenance1、create_renderpass2、imageless_framebuffer、timeline_semaphore；
descriptor_update_template已经可用。其他GL基础features仍不完整，见Issue #32。

本地环境为x86_64 Linux + 原生Mesa25.0.7 Lavapipe/LLVM19软件Vulkan，guest代码仍为
AArch64，经同一解释器/NativeCall路径执行。此结果不是Apple GPU或iPad验收。
只有guest ICD、bundle manifest常量和相关测试/构建文件改变，CPU/MMU/native GPU
backend/UI无改动，复用此前已通过验收；不重跑CoreMark、Docker或全套CPU/网络测试。

精确源码Linux CI重建原版guest ICD、执行新基础检查与原版Mesa probe。
Apple CI只检查manifest更新和iOS embedding编译，**不等同新增Metal GPU/GL测试**。
Apple/iOS metadata CI在a231f47已通过（run39202469057），只做两项manifest测试与
iOS FFI编译，不是Metal GPU验收。Linux run39202468982在干净SDK链接阶段发现
准备脚本只收集`.so.*`而遗漏裸soname的`libgallium-25.1.9.so`及非标准名字的SONAME链接；已修复并以最小
sysroot验证链接。没有runtime改动，Apple源码/命令与已通过结果精确核对后复用，
只重跑未完成的Linux检查。第二次干净CI run39203103689暴露LLVM子目录库链接
扁平化后指向自身；准备脚本跳过同名别名，并在写入真实库前移除旧链接。
全新目录重新收集381个共享库文件，199个别名均可解析；只使用该目录及最小musl
CRT/libc sysroot成功链接EGL客户端，避免本地完整SDK掩盖依赖缺失。
第三次CI run39203619165的SDK、Rust检查、两项manifest测试、release构建与
五种冷进程启动检查通过。native查询测试把unknown group pNext保留也施加给宿主，
但CI默认Mesa `device_select`隐式层会覆盖它；这是参考测试的环境假设。
用原版Ubuntu Mesa25.2.8及原版loader1.3.275/本地1.4.309加相同层独立复现：
旧断言均abort，新测试均通过。native只要求合法设备成员和能力，guest继续严格检查
pNext/canary、零外部handle与受限profile；单独重跑修改的guest查询通过。
生产Rust/ICD与a231f47保持相同，本次已通过的Linux bundle/reopen检查和
Apple metadata检查均按源码、命令与成功步骤核对后复用。

最终精确源码1e0aac5的[Linux run39204706189](https://github.com/kb5000/trames/actions/runs/39204706189)
通过，含原版签名包安装、native/guest查询、legacy/template GPU计算、纹理方程和
四张导出图逐字节对照。CI参考为Ubuntu Mesa25.2.8 Lavapipe软件Vulkan。
artifact12164804745的SHA-256与ZIP CRC已校验；结果、原始stdout/stderr、输入摘要和
图像摘要持久保留在`zink-foundation/ci-linux/`。原版Mesa EGL probe仍exit1，
`rendering_passed:false`，缺少的四个硬依赖与本地一致；未打IPA或发布。

复现入口：

```sh
bash scripts/build-vulkan-fixtures.sh
python3 scripts/prepare-zink-guest.py
# VK_DRIVER_FILES在宿主选择实际driver；guest driver由测试单独指定。
# 本地参考是Lavapipe时设置TRAMES_ZINK_CPU_REFERENCE=1。
python3 tests/integration/zink-foundation.py
python3 tests/integration/zink-foundation.py --require-render --output target/zink-required
```

原始记录、软件输入摘要与图像摘要见同目录`zink-foundation/`。
