# 原版Mesa Wayland EGL窗口验证

沿main单线推进，基线`6c7103e`。第一轮使用原版Mesa25.1.9；后续只给EGL平台组件
增加DRM-less native WSI适配。原版Gallium/Zink/NIR/GLSL/SPIR-V/GLES2、Vulkan loader及
libwayland未改。CPU/MMU、Linux、网络、合成器/UI未改；已通过的离屏与其他模块结果复用。

本地Linux参考（Mesa25.0.7 Lavapipe/LLVM19软件Vulkan）已通过：

- 生产session FFI和原生Wayland compositor中的两个原版Mesa EGL/GLES2窗口，
  经Kopper→guest ICD→native Vulkan swapchain呈现；捕获的全部scene图像为GPU kind2，
  未回退到SHM像素上传。
- 两次启动，窗口尺寸分别96×64→160×96、80×48→128×72→144×80；各新尺寸的完整
  四象限GLSL像素检查通过。关闭第一个窗口后第二个仍能绘制/resize/关闭，最终无残留窗口。
- 保留旧帧引用跨swapchain替换与两个窗口/guest进程销毁，元数据仍有效后才释放。
  采集次数不是渲染帧数，结果另存唯一scene revision数量；本批不是性能跑分。
- 独立标准Vulkan客户端在native参考上验证64个GPU像素：偏移、多region以及同一图片
  不重叠区域的原生`vkCmdCopyImage`。同一客户端在guest下也通过并正确清理。
- 18项Vulkan单元测试、严格Clippy全部targets通过；新边界测试覆盖复制偏移溢出、
  不支持的mip/layer及深度格式不兼容。source/destination由录制/提交依赖图保活。

本批修复swapchain采样usage被拒绝，以及Kopper后续resize调用缺失`vkCmdCopyImage`
而跳到空函数指针的崩溃。复制仅支持当前profile的一层/一个mip的2D图像；颜色格式保持
相同32位大小，深度格式必须一致。拒绝越界、无绑定、错误usage/layout、同图片重叠区域。
新wire op134，revision8；既有wire布局保持兼容。

Mesa25.1.9的ES2 GL-thread内部ancillary invalidate入口标为ES3，交换缓冲区后会产生
`GL_INVALID_OPERATION`。窗口验证使用其上游配置`mesa_glthread=false`，没有修改GL-thread实现，
没有伪造ES3或用软件OpenGL替代Zink。Linux CPU physical device另用标准
`LIBGL_ALWAYS_SOFTWARE=true`参考选项；Apple硬件验收不使用该选项。

[本地结果/日志](zink-wayland/)已归档。[第一轮CI39254466978](https://github.com/kb5000/trames/actions/runs/39254466978)
的Linux全新签名APK安装、两轮窗口流程通过。Apple模块/iOS编译通过，同一提交457文件
的guest输入逐项SHA-256通过；但原版EGL因没有DRM设备退到SHM，被GPU kind2硬门槛正确拒绝。
不能把这次失败算作Metal原生呈现通过。Linux artifact摘要与ZIP CRC已验证，输出保留在
[ci-linux](zink-wayland/ci-linux/)。

为保留GPU-only窗口，EGL适配只增加显式的`LIBGL_KOPPER_WAYLAND_NO_DRM`路由和
真实native EGLDevice身份；不报告DRM/软件设备扩展，不伪造Vulkan扩展，不改核心算法。
仅重编译EGL并链接原版签名Gallium，保留标准`libEGL.so.1` SONAME。生产FFI在开启Vulkan、
有随包组件时自动原子安装到`/usr/local/lib`并设置环境，apk管理的Mesa文件保持原样。
新增安装/更新/拒绝坏输入/保持原始Mesa文件的模块检查通过；本地自动安装、设备身份/枚举、
两轮窗口/resize/close与GPU-only场景也通过，见[组件本地记录](zink-wayland/egl-adapter-local/)。
固定原始源码摘要、补丁与原版Gallium摘要、交叉编译参数、组件摘要及许可随产物保留。

[EGL适配后Linux CI39260974313](https://github.com/kb5000/trames/actions/runs/39260974313)
的干净环境构建、签名APK安装、生产自动安装、设备身份/枚举、两轮窗口流程均通过。
[归档](zink-wayland/ci-linux-adapter/)包含全部输出、EGL组件manifest、459文件bundle的
逐文件SHA-256与ZIP验证。组件仅446KiB，原版Gallium仍来自签名APK；隔离Python环境的
固定构建依赖见requirements，原始源码与上游算法不变。原Vulkan模块源码完全未变，
直接复用b23e75a的Linux/Apple严格Clippy及18项测试。

Apple使用相同459文件bundle；只允许CI/文档变更时复用Linux通过记录，验证器仍检查原始
source SHA、当前commit及每一个文件摘要，不把旧bundle标成新源码。
[Apple重试39262670141](https://github.com/kb5000/trames/actions/runs/39262670141)
的本轮distro/FFI严格Clippy、安装模块3项及iOS编译通过。未改动依赖的AArch64专用lint不在
本轮范围，使用Clippy的--no-deps；没有关闭本轮代码的警告检查。随后原生窗口测试在首个swapchain创建时崩溃；[LLDB证据](zink-wayland/native-memory-metal-failure/)
定位到vkBindImageMemory→MoltenVK初始host上传→Paravirtual Metal replaceRegion。
旧策略只按DEVICE_LOCAL排序，在统一内存GPU上会选到同样local的host-visible类型。
窗口图片仅供GPU使用，现优先private device memory，无此类型时保留原有合法fallback。
[修复18162c9](https://github.com/kb5000/trames/commit/18162c9c53ba1d8334b227087cbfd1474633bb2e)
的18项测试/严格Clippy在本地通过；因原生分配路径改变，本轮重验Linux/Apple图形，
其余CPU/MMU/Linux系统调用/网络/UI检查继续复用。

[最终CI39264897300](https://github.com/kb5000/trames/actions/runs/39264897300)全部通过，
运行源码为18162c9。Linux与Apple严格Clippy/18项Vulkan测试、安装模块3项和iOS FFI编译通过。
Linux窗口结果见[ci-linux-private](zink-wayland/ci-linux-private/)，
Apple真实MoltenVK/Paravirtual Metal结果见[ci-metal](zink-wayland/ci-metal/)。
Apple逐一校验同一提交的459文件及其SHA-256；两边的ICD、EGL组件和输入store摘要一致。
两份结果ZIP的SHA-256和全部CRC均已验证。EGL组件manifest与新bundle中的对应文件摘要
完全相同，避免将本地不同编译器的组件误记成CI产物。

Apple两轮各验证96×64、80×48、160×96、128×72、144×80的全部导出MTLTexture像素，
颜色、alpha、原点方向均正确；同一旧帧跨swapchain替换、窗口及guest进程销毁后再次全像素
校验通过。所有scene均为GPU kind2，正常呈现不回退SHM/CPU像素上传。
测试oracle单独用Metal blit读回共享buffer验证，生产呈现仍直接采样持有的GPU texture。
逐窗口最小化/还原/关闭、剩余窗口继续resize、第二轮重启及最终清理均通过。
采集次数不当作渲染帧数，唯一scene revision另列；没有宣称帧率或物理iPad性能。

物理iPad未验收，未构建IPA或发布release。
