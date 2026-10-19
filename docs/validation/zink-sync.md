# Zink maintenance1、timeline 与 driver properties

本轮在main单线开发，基线bea7fd9。实例仍为1.1，physical device仍为受限1.0 profile。
新增8个公开入口，共151个；不声明完整Vulkan或OpenGL一致性。

maintenance1原生启用、负/零高度viewport与TrimCommandPool；timeline feature/device启用、
64位初始值/计数器/host signal、wait-any/all与timeout、混合binary/timeline提交；
driver ID/name/info与timeline最大值差均来自原版driver。原生pNext只在宿主构造，
guest只收标量和数组。bridge的conformanceVersion置零。

原生driver允许vkQueueSubmit阻塞在未来host signal。本实现先检测没有入队来源的未来
等待值：NativeCall内部返回NOT_READY，由guest ICD轮询并nanosleep，让其他guest线程
运行并signal；该内部状态不会作为vkQueueSubmit的最终结果返回。已入队GPU signal与
同次提交中更早的signal计入依赖，避免不必要的CPU等待。原生driver继续执行真实同步。

最后一批timeline signal满足后，命令缓冲可以reset/re-record；私有fence和提交时的资源
快照保持到native fence完成，稍后回收不会再次改写新命令状态。WSI只接受binary信号量。

本地原版Mesa25.0.7 Lavapipe/LLVM19软件Vulkan验证：

| 项目 | 结果 |
| --- | --- |
| 17项Vulkan单元测试、严格Clippy | 通过 |
| 既有native lifecycle、取消编译、未发布资源回滚与在途依赖 | 通过 |
| 原版loader查询driver identity与timeline properties | 通过 |
| 2^40初始值、计数器、host signal、wait-any/all、有限timeout与跨线程唤醒 | 通过 |
| 1核/4核guest，未来host signal、binary混用、同批及先前提交的timeline依赖 | 通过 |
| 原生GPU写入、coherent映射可见性、wait后立即pool reset/trim | 通过 |
| 动态/静态负viewport、正viewport回归，三轮独立像素方程 | 与原生导出图逐字节一致 |
| 原有compute/descriptor-template/texture输出 | 通过 |
| 原版Alpine Mesa25.1.9 EGL context | **仍失败，未产生GL画面** |

实测未实现硬依赖剩VK_KHR_create_renderpass2和VK_KHR_imageless_framebuffer。
后续还需设备features、图像/格式/渲染状态等，不能把两项扩展名称补齐视为GL已可用。
未改Mesa或以其他OpenGL实现替代。物理iPad上新功能仍待验收，本轮未打IPA或release。

CI只验证受影响的Vulkan backend/ICD和iOS embedding；CPU/MMU、网络、UI与bundle
安装源码未变，复用已有验收。Apple CI新增原版MoltenVK的native timeline例程，检查
真实GPU填充/读回及同步；该检查已通过，运行范围见下方精确CI记录。

本地结果、stdout/stderr和图像摘要见同目录`zink-sync/`；原版输入仍沿用
`zink-foundation/inputs.json`。新增wire revision5保持旧8/54/56操作兼容，扩展
8的可选启用flags、54的可选type/initialValue，新增115..121能力/trim/timeline操作。

```sh
bash scripts/build-vulkan-fixtures.sh
python3 scripts/prepare-zink-guest.py
cargo run -p trames-vulkan --example timeline --release --locked
python3 tests/integration/zink-foundation.py
```

精确源码3e6c014的[Linux CI39221867948](https://github.com/kb5000/trames/actions/runs/39221867948)
通过，包含native timeline/lifecycle、17项单元测试、严格Clippy，以及上述1/4核和像素
对照。artifact12172386325的SHA-256及ZIP CRC已核对，原始输出与摘要见`zink-sync/ci-linux/`。
Apple run39221868006的模块检查和iOS FFI编译通过；macOS原生例程在链接阶段缺少IOKit与
Clang availability runtime，尚未执行GPU。本次只给CI native harness补这两个依赖，最低
macOS版本对齐原始MoltenVK archive的12.0，不修改iPad链接或生产runtime。
重试前逐文件、命令及GitHub成功步骤核对，复用Linux整体验证与Apple模块/iOS检查。

生命周期依据：[Vulkan1.3.281同步规范](https://github.com/KhronosGroup/Vulkan-Docs/blob/v1.3.281/chapters/synchronization.adoc)
VUID-VkSemaphoreSignalInfo-value-03259要求host signal值小于所有pending signal值；
合法应用不能用host signal越过尚未完成的GPU signal。观察到最终GPU timeline值因此
可确认此前命令完成，私有fence继续单独保活。仍由应用遵循Vulkan原本的同步责任。

[Apple CI39222826919](https://github.com/kb5000/trames/actions/runs/39222826919)在CI-only
提交daabd76通过。生产runtime与3e6c014逐字节相同，模块/iOS及Linux成功结果按源码、
命令与GitHub成功步骤复用，没有重跑。原版MoltenVK1.4.2识别Apple Paravirtual device，
Metal Shading Language3.2，semaphores使用MTLEvent；native timeline例程完成真实GPU
填充/读回、2^40计数器、非阻塞未来等待、wait后reset/trim与资源清理。
macOS GPU通过不等同物理iPad或新增GL/Wayland窗口验收；负viewport的像素对照仍是
Linux/Lavapipe记录。Apple原始模块/iOS与GPU输出见`zink-sync/apple-*.log`。
