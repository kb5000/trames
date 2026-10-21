# Zink 图像、mipmap 与子资源

2026-10-21，接续 [MRT/格式批次](zink-es3.md)。保持原版 Mesa 25.1.9、
GLES/Zink/SPIR-V 算法与原版 Vulkan loader，不修改 GL 版本或扩展判定。

## 本批实现

- 原生 optimal、单 sample 图像支持 1D/2D/3D、array/cube 与 mip 链。
  限制取宿主实际 format properties 与本桥接上限的交集：4096 维、2048 层，
  mip 数还受每张图像尺寸限制。创建与查询使用相同的 image-format-list 路径。
  imageCubeArray 只按原生 feature 查询，并在 device 创建时显式启用。
- 视图保留 base/count mip 与 layer，解析 `VK_REMAINING_*`，验证维度、cube
  兼容标记、层数和实际范围；选定 mip 的尺寸用于 framebuffer 验证。
  maintenance1 的 3D→2D/array 切片视图还要求真实 native portability feature；
  当前 framebuffer 仍为单层、单 mip 附件，不宣称 layered rendering。
- 图像屏障和 color/depth clear 使用指定子资源范围。buffer/image copy 支持
  array 与 volume，按实际 texel 大小计算行间距、层/切片间距、最后一行与溢出边界。
  image/image copy 支持对应的 mip/layer/volume；同图不同 mip、层或不重叠区域
  可以复制，仍拒绝所有 region 之间的源/目标交叉重叠。
- 新增原生 `vkCmdBlitImage`：nearest/linear、缩放/翻转、mip 生成、真实 driver
  支持的颜色格式转换。保持 signed/unsigned 与 depth 类型规则；深度要求同格式
  和 nearest。两端图像类型相同，混合 2D↔3D blit/copy 尚未实现。
- 新增 `vkCmdClearDepthStencilImage`，当前支持 D16/D32 的 depth aspect；
  stencil 仍不可用。比较 sampler 保留 compareEnable/compareOp，仍拒绝
  unnormalized coordinates。比较 sampler 按宿主实际 mutableComparisonSamplers
  feature 查询/启用，不支持时明确拒绝；3D slice 兼容创建标记也在查询与创建时
  检查实际 portability feature。sampler compare 的本批 native 验证为八种操作创建，
  尚未增加 shadow-sampling 像素检查。
- 所有数组先从 guest 复制成有界 native 记录；guest 指针不进入 driver。
  命令和提交快照保留 image/view/memory，删除 guest 句柄后不提前释放在途资源。
  没有增加解释器访存检查、整次 GPU 操作锁或新的 CPU 内存屏障。

wire 保持旧客户端：op60 在 a11 非零时带 type/depth/mips/layers，op64 的 a7
指向复制的五字段 subresource range；旧调用仍默认 2D 单 mip/layer。op44 新增
10-word barrier 记录，旧 5/6-word 格式继续接受；op92 追加比较参数。新 op135
为 blit、op136 为 depth clear，公开 ICD 入口由 172 增至 174。

## Linux 本地验证

开发宿主为 x86_64 Linux，真实 Lavapipe Vulkan 参考设备。它是 CPU physical
device，只在显式 `TRAMES_ZINK_CPU_REFERENCE=1` 下让原版 Zink 选择，不能替代
Apple Metal 或物理 iPad 验收。

- Vulkan 23 项测试、严格 Release Clippy（全部 targets）、Rust 格式、C fixture
  `-Wall -Wextra -Werror`、Python 语法与 diff 检查通过。
- 原生 GPU 读回覆盖 1D array、12 层 cube-compatible 2D、3D volume 的 mip、
  不同 layer/z slice 的同图复制、mip blit、只清除最后一 mip 的一个 layer、
  局部 D32 clear。逐字节检查基准层、下一 mip 与未写入 padding；提交后先删除
  图像与 backing，再等待 fence，验证在途保活。
- 原生 cube/cube-array view、选定非零 mip/layer 的 2D attachment view 通过；
  过大的 framebuffer 正确失败。另将 3D mip1/slice1 作为 2D attachment，实际
  渲染并复制回读全部四个蓝色像素，2D-array slice view 也通过。
- 原版 Mesa 的 dense/sparse MRT 仍通过 512 像素。新纹理 fixture 检查手动上传的
  各 mip 不同颜色、重生成 mip 后的采样值，以及六个 cube 面的 LOD2 结果。
  显式 cube LOD 使用核心 GLSL ES 1.00 的 vertex texture sampling，不要求当前
  未公布的 fragment `EXT_shader_texture_lod`，没有覆盖扩展字符串。
- 初测确实发现 Mesa 读回提交 BGRA→RGBA blit；原先“只接受相同格式”会失败。
  完成原生格式转换后，同一原版客户端通过。没有用复制原始 bytes 冒充转换。
- 最终 `--stage all` 中 MRT 与 textures 成功；ES3 仍正确失败，原版 EGL 返回
  ES3 configs=0，整体脚本退出 1。没有把两项图形 gate 成功算成完整 GLES3。

```sh
python3 tests/integration/zink-es3.py \
  --store target/zink-inputs/store --output target/zink-images --stage graphics
# 另外显式探测仍缺少的 ES3；当前预期返回失败
python3 tests/integration/zink-es3.py \
  --store target/zink-inputs/store --output target/zink-es3 --stage es3
```

脚本直接打开显式临时 store，不复制整个 rootfs；只导入唯一 probe 路径，最后
清理。4 CPU，总内存与单进程各 4 GiB，离线。CI 对应的 Linux/Apple 原生图像
检查和原版 Mesa 图形 gate 使用同源 ICD、fixture、签名 Mesa store 和逐文件
hash 清单。Apple 首轮确认 1D/2D 路径与原有 MRT/格式检查通过，但 3D mip
缩小读回失败，定位为上游取错 mip 深度；见 [原版源码最小修正](moltenvk-source-build.md)。
修正后已通过下述 CI；没有 IPA，不上传本地 Firefox 诊断。

## Apple 最终结果

修正源码 [a410f1c](https://github.com/kb5000/trames/commit/a410f1c4e8a2860cc9bd350bf49c8db14456ddb9)
的 [CI 39551642471](https://github.com/kb5000/trames/actions/runs/39551642471) 全部成功。
使用实际 Apple Paravirtual GPU、从原版源码构建的 MoltenVK；CPU-reference 标记为 false。

- 8 色加 depth、11 种格式往返、1D/2D array、cube/cube-array views、3D volume、
  mip blit、layer/slice 同图复制、局部 color/depth clear 与未写入 padding 全部正确。
- 原先失败的 3D mip 缩小恢复精确结果；非零 3D mip/slice 的 2D attachment
  实际渲染与读回也通过。原版 Mesa dense/sparse MRT、2D mip 选择/生成及六面 cube
  LOD2 采样成功，probe 清理成功。没有用版本/扩展覆盖把 GLES2 改成 GLES3。
- 同一最小修正成功构建 arm64 iOS 原生库。工具链实际为 Xcode 16.4，macOS SDK
  15.5 / iOS SDK 18.5；不是物理 iPad 验收，也没有打包 App。
- guest/backend/fixture 与 Rust toolchain 精确复用 ebace64 的
  [39547799850](https://github.com/kb5000/trames/actions/runs/39547799850) Linux GPU/Mesa
  和 Apple module/23 tests/iOS FFI 检查。只重跑受影响的 native Metal 与 iOS 原生构建。
- 18 项证据 ZIP 的 SHA-256 与每项 CRC 已独立核对；两个原生 build record 的
  source/patch/recipe/manifest 摘要与仓库匹配。[摘要与原生库 hash](zink-images-ci.json)。
  此后只整理文档/验证计划，复用运行源码的通过结果。

## 尚未完成

完整 GLES3 仍需要 GPU transform feedback 及其余真实 features/状态；MoltenVK
1.4.2 自身没有 `VK_EXT_transform_feedback`，不能靠多转发一个函数解决。
原计划继续复用上游 SPIRV-Cross/MoltenVK vertex-as-compute capture-output
基础，覆盖布局、顺序、counter、暂停/恢复与可见性后再公布能力。

压缩块、stencil、MSAA/resolve、多 subpass、layered framebuffer、更多扩展和
完整 Vulkan 一致性仍在后续范围。Firefox 的代理连接认证和首帧 present 交接
也尚未解决；本批不修改浏览器默认 proxy、内容沙箱或刷新率。
