# Firefox GLES3：代理认证与绘制状态

2026-10-21，接续 [Firefox 交互调查](firefox-interaction.md)及
[Apple 原生 TF/GLES3](vulkan-native-tf-runtime.md)。本轮保留原版签名 Alpine
ARM64 Firefox 142.0-r0、Mesa 25.1.9、内容沙箱和默认 Wayland proxy，使用生产
Release App FFI，4 CPU，总提交及单地址空间各 4 GiB，离线页面。

Firefox 自身的 `glxtest --wayland` 已能取得真实 GLES3。Linux 参考的 physical
device 是 Lavapipe CPU，工具如实报告非硬件加速；Firefox 默认仍会选择软件
渲染。另用独立新 profile 的 `gfx.webrender.all=true` 检查原版 WebRender 的
OpenGL 路径，保留默认刷新率。这是显式诊断，不是修改产品默认偏好或伪造设备。

**完整 Firefox GPU 页面尚未通过。** 定向组先后越过 descriptor 批量分配和
sample-rate shading 的错误。随后 Mesa 驱动线程的空地址调用已对照原版代码确认
为缺少 `vkCmdClearAttachments`，现已补齐并越过；下一处失败是原版 Zink 创建
render pass 的状态尚不被接受。仍未取得目标 canvas 或新的浏览器 GPU 呈现结果。
不能把 GLES3、标题、窗口存在或独立 Mesa 用例通过记成 Firefox/WebRender 已可用。

## 已修复的通用路径

- 代理后的 Wayland FD 不再被错误地当作直连 channel。内部
  `trames_native_wsi_v1` 协议将空匿名文件通过普通 SCM_RIGHTS 送到合成器，
  用同一 retained shared inode 绑定真实连接。NativeCall 只能通过该文件能力
  查找这一连接的 surface；文件内容、guest FD 数字或单独 object ID 无效。
  重复跨连接授权会失败，对象/连接销毁撤销授权，发布快照也保留 inode 防复用。
  普通直连继续原有快路；授权仅用于创建/查询，不进入逐帧像素或普通访存路径。
- ICD 用独立 libwayland queue 发现和绑定内部协议，不在 Vulkan 调用中执行
  GTK/Firefox 的应用队列回调。无需关闭或修改 Firefox 自身的透明代理。
- 原版 Zink descriptor cache 从小批量增长到一次 100 个 set，旧桥接层限制
  为 64，导致申请失败并崩溃。申请及释放批量现与已有 pool 的 4096 上限一致，
  保留全局对象上限、输出预检和原生 pool/资源生命周期。
- 单采样 pipeline 保留原始 `pSampleMask`，包含零 mask；未使用的高位允许
  任意值。旧 ICD 保持原有全覆盖默认值。
- `sampleRateShading` 按原生能力查询，在 device 创建时显式请求/启用；
  `sampleShadingEnable` 和有效的 `minSampleShading` 在异步编译前复制为宿主值。
  没有 guest 指针进入编译线程。图片仍限单 sample，未据此宣告 MSAA。
- `vkCmdClearAttachments` 接入原生 render pass 内的批量局部清除，保留 subpass
  颜色槽编号和 clear-value 原始位，支持当前单层 profile 的颜色/深度/模板 aspect。
  清除矩形必须位于实际 render area 内；开始 pass 已保留的附件依赖沿用到提交完成。
  公共 Vulkan 入口增至 189；新增 wire op 151，profile 0x10006/wire 8 不变。

## 定向验证

首批 Linux Release 的 Vulkan 27 项、Wayland 12 项及相关模块严格 Clippy 通过；
增加 attachment-clear 后 Vulkan 28 项和严格 Clippy 通过，Wayland 结果按源码复用。
WSI 新回归覆盖 inode 身份、无关文件、跨连接重复授权、对象销毁和断连撤销。

原版 ARM64 Mesa/libwayland 经 guest socket pair 的透明 byte/SCM_RIGHTS 代理，
两次启动均完成双窗口 GPU 绘制、resize、close 和旧图像保活。窗口客户端检查
实际 GL 像素，前端检查 native GPU 图像类型；Linux CPU Vulkan 参考不冒充 iPad。

独立 Vulkan fixture 一次申请 100 个不同 set，使用最后一个完成 128 个 GPU
输出检查，并验证批量 free/reallocate/reset 和输出边界。采样 fixture 检查
mask=1、mask=0、仅无效高位及显式 sample-rate shading；分别验证真实绘制或
保持清屏色。旧图形/TF 数据验收、CPU/Memory/Linux/网络/UIKit 源码未变的
通过结果继续复用。

首批源码 [10bdafa](https://github.com/kb5000/trames/commit/10bdafa8fd963387825b8de53aabf9803fa09be1)
的 [CI 39611594760](https://github.com/kb5000/trames/actions/runs/39611594760) 已在
Linux 与云端 Apple Metal 全部通过，包含同一批 descriptor/sample GPU 数据、
透明代理后的双窗口导出 Metal 纹理逐像素检查、旧帧保活及 iOS FFI 编译。物理
iPad 尚待验收，未用 Linux CPU reference 冒充 Metal。该 CI 没有 Firefox 输入。
后续只新增 attachment-clear 的矩形/颜色/alpha/scissor 像素检查及相关 Vulkan
模块验证，复用已通过且源码未变的代理、采样、TF 和其他模块，不重复整套窗口测试。

复查入口：先用 `prepare-zink-guest.py --wayland` 准备原版签名包和客户端，
再运行 `tests/integration/zink-wayland.py --relay --output <新目录>`。
`--clear-only` 仅检查新增的 render-pass 局部清除；其余通过结果需独立引用。
`--in-place-store --store <临时 store>` 可避免复制 rootfs，但不得同时运行其他
使用该 store 的 session。Apple 还要求已有的 exported-MTLTexture 像素 oracle。

Firefox 原始日志、截图、地址、线程栈及运行转储仅保留本地；临时故障采集不进入
生产 Core。[Issue #33](https://github.com/kb5000/trames/issues/33)保持开放。
本轮没有构建 IPA。
