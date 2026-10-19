# 原版Mesa EGL/GLSL与资源扩展验证

本批沿main单线推进，基线`3450c2f`。Mesa25.1.9与Alpine Vulkan loader未改；
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

[本地原始输出与结果](zink-gles/)已保留。Apple将复用同一提交的Linux guest二进制和原版Mesa
store；逐文件SHA-256及源码commit匹配后才在原版MoltenVK/Metal上运行资源例程和原版Mesa GLSL。
本批Linux/Metal CI结果待回填；物理iPad未验收。

仍有custom-border-color、line-rasterization、depth-clip-enable等上游警告；测试只覆盖这些
离屏基础行为，不声明完整OpenGL、Vulkan一致性或Wayland EGL窗口已可用。没有构建IPA或release。
