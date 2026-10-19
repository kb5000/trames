# 原版Mesa EGL/GLSL与资源扩展验证

本批沿main单线推进，基线`3450c2f`，运行源码
[`c15d6a5`](https://github.com/kb5000/trames/commit/c15d6a586f12995b8d9b5e491938d67ab6980f07)。
Mesa25.1.9与Alpine Vulkan loader未改；
新增171入口profile的布局/资源/渲染支持，未改CPU/MMU、Linux、网络或App UI。

本地已通过：

- 原版Mesa EGL1.5、GLES2 context、64个GPU清屏像素、64个uniform GLSL像素，
  三轮共192个原生纹理采样像素及销毁。`--require-render`作为强制门槛通过。
- 52项整合case全部exit0；20张native/guest图片逐字节相同。新增compatible-renderpass/
  全核心dynamic-state三帧重心方程检查，以及两种descriptor写入方式的资源GPU检查。
- 原生`resources`例程校验动态UBO偏移、texel读写、scalar vec3位于offset4、storage image、
  pipeline cache往返/INCOMPLETE/merge、pool reset后失效旧set并重分配/执行。
  两轮256个结果正确；越界动态偏移被拒绝，最终预算归零。
- Clippy全部targets、17项Vulkan单元测试通过。原生参考是Mesa25.0.7 Lavapipe/LLVM19软件Vulkan，
  不作为iPad GPU性能结果。

[本地原始输出与结果](zink-gles/)已保留。

[Linux/Metal CI39240757068](https://github.com/kb5000/trames/actions/runs/39240757068)全部通过：

- Linux严格Clippy全部targets、17项单元测试、原生timeline/renderpass/resources/lifecycle
  均通过。全新Alpine签名Mesa安装及整合共53项case全部exit0，20张native/guest图片
  逐字节相同；原版Mesa完成clear、uniform GLSL与三轮纹理采样，`--require-render`通过。
- Apple模块Clippy、17项单元测试和`aarch64-apple-ios` FFI编译通过。无需重新编译guest：
  源码commit和448个guest ICD/Mesa store/客户端/shader文件逐项SHA-256校验通过。
  两端ICD摘要与输入store manifest摘要一致；[完整清单](zink-gles/guest-bundle-checksums.json)。
- 原版MoltenVK1.4.2在Apple Paravirtual device通过原生timeline、renderpass/imageless与
  resources例程；后者两轮共256个GPU结果正确，cache往返、pool reset/reuse通过，预算归零。
- 同一ARM64客户端经解释器、原版Mesa/Zink和guest ICD，在Metal上完成EGL1.5/GLES2
  context、64个清屏像素、64个uniform GLSL像素、三轮共192个纹理采样像素与销毁。
  实际GL renderer为`zink Vulkan 1.0(Apple Paravirtual device (MOLTENVK))`。
  两个探针/清理case全部exit0，`rendering_passed=true`。
- Linux和Metal evidence ZIP均已下载，SHA-256匹配GitHub artifact digest，全部ZIP CRC通过。
  [artifact与源码元数据](zink-gles/ci-artifacts.json)保留精确摘要；图片用摘要留档，未提交二进制。

[Linux整合输出](zink-gles/ci-linux/) · [Linux检查日志](zink-gles/linux-checks.log) ·
[Metal原版Mesa输出](zink-gles/ci-metal/) · [Metal原生例程](zink-gles/ci-metal-native/) ·
[Apple检查日志](zink-gles/apple.log)。物理iPad未验收。

仍有custom-border-color、line-rasterization、depth-clip-enable等上游警告；Apple当前driver也
没有可用的logicOp，并报告Metal无法禁用primitive restart。当前样例像素正确，不代表
相关裁剪、逻辑操作或带restart索引的绘制已可用，后续需要分别补齐/验证。测试只覆盖这些
离屏基础行为，不声明完整OpenGL、Vulkan一致性或Wayland EGL窗口已可用。
CPU/MMU、Linux、网络和UI源码未改，已有通过记录复用。没有构建IPA或release。
