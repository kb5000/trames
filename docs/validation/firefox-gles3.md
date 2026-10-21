# Firefox GLES3：代理认证与绘制状态

2026-10-21，接续 [Firefox 交互调查](firefox-interaction.md)及
[Apple 原生 TF/GLES3](vulkan-native-tf-runtime.md)。本轮保留原版签名 Alpine
ARM64 Firefox 142.0-r0、Mesa 25.1.9、内容沙箱和默认 Wayland proxy，使用生产
Release App FFI，4 CPU，总提交及单地址空间各 4 GiB，离线页面。

Firefox 自身的 `glxtest --wayland` 已能取得真实 GLES3。Linux 参考的 physical
device 是 Lavapipe CPU，工具如实报告非硬件加速；Firefox 默认仍会选择软件
渲染。另用独立新 profile 的 `gfx.webrender.all=true` 检查原版 WebRender 的
OpenGL 路径，保留默认刷新率。这是显式诊断，不是修改产品默认偏好或伪造设备。

**当前默认刷新率的原生 GPU 页面、输入及 resize 已通过本地参考；正常退出仍待修复。**
以下保留逐步定位记录，后续状态见“默认刷新率、viewport 与调整大小”及退出调查。
定向组先后越过 descriptor 批量分配和 sample-rate shading 的错误。Mesa 驱动
线程的空地址调用对照原版代码确认为缺少 `vkCmdClearAttachments`，现已补齐。
随后 renderpass2 对输出引用 `aspectMask` 的误判也已修正：该字段只对 input
attachment 有意义，颜色/深度输出引用忽略它，并非缺少另一项渲染能力。

修正后 JS/title-only gate 通过，但仅保留旧 SHM 图像，没有 canvas 像素验收。
新增 `--require-gpu-frame` 后保留同一配置观察 480 秒：目标 JS 已执行，未取得
该目标窗口的 native GPU 图像，gate 正确失败，结束诊断完整，浏览器未退出；
没有重现此前的 API 崩溃。此组的周期日志导出明确属于诊断，不用作性能比较。
下一步追踪 EGL swap、queue present、合成器帧交接及刷新任务，不能把标题或
独立 Mesa 用例通过记成 Firefox/WebRender 已可用。

## 首次原生呈现与回调提交

同日继续追踪原版 Firefox 的呈现调用，确认 `wr_renderer_render` 已成功返回，
`RenderCompositorEGL::EndFrame` 和 EGL damage swap 也实际执行。缺口位于 ICD：
Mesa 为局部更新附加 `VkPresentRegionsKHR`，旧的通用 `pNext` 检查在 NativeCall
之前拒绝了它。不能根据宿主未收到 op 91 就断言浏览器没有调用 present。

现支持 `VK_KHR_incremental_present`。更新矩形是允许忽略的优化提示，当前原生
WSI 仍呈现完整的 retained image；只在 guest 侧解析链和结构，不把矩形指针送进
宿主，也不加入像素复制。重复/未知链、计数不匹配和非空计数的空数组继续拒绝。
原版 Firefox 在同一 Linux CPU Vulkan 参考中首次实际提交 present，前端取得
kind=2 的原生 GPU 图像。该组观察 300 秒仍没有目标页面 JS 标题，不能把它算作
完整页面或 canvas 像素通过。后续原生像素及默认刷新率交互仍在排查。

另修正两处通用 Wayland 回调行为：父场景的 presentation 不再完成没有图像的
子树及已 dismissed 表面的回调；`wl_surface.frame` 加空 commit 只请求定时回调，
不再发布相同图像的新场景。已经显示内容的回调继续由现有 60 Hz pacer 定时完成，
不依赖 UI 重绘；未显示的内容继续等待真实 presentation。明确记录 `damage` /
`damage_buffer`，确保原 buffer 原地更新仍发布新内容，同步子表面的缓存合并保留
damage。没有修改 Firefox 偏好、guest 时钟或 CPU 调度策略来隐藏回调问题。

新增标准 wire peer 回归先在旧实现在父帧完成时收到全部五个回调，预期只有两个；
修复后还检查空子树首次映射、无重绘的延迟回调及无新 attach 的 damage 提交。
Wayland 13 项及严格 Clippy 通过。原版 Mesa 客户端新增 damage-swap 模式，每次
提交两个更新区域；`--damage-only` 仅跑这一项双窗口、调整大小、关闭和旧纹理
保活验收，复用未变的 descriptor/sample/clear/TF 用例，Apple 仍要求导出纹理的
逐像素 oracle。原始 Firefox 诊断及临时采集代码不提交，尚不构建 IPA。

该源码 [5f6f905](https://github.com/kb5000/trames/commit/5f6f905e13de4d367cfb033d560af3b9cc7bfe58)
的 [CI 39624044606](https://github.com/kb5000/trames/actions/runs/39624044606) 已在
Linux 与云端 Apple Metal 完成新 damage-swap 用例。Apple 导出纹理逐像素检查
覆盖双窗口、五种尺寸及旧图像保活，Wayland 13 项和 iOS FFI 编译通过。复用
未变的 Vulkan/TF/descriptor/sample/clear 验证；这些公开证据只有合成测试数据。

## Canvas 映射额度与 GPU 页面诊断

继续推进的低刷新率诊断到达 CanvasRenderer 后，原生映射返回内存不足，浏览器
随后退出。原来的 128 MiB 是跨地址空间共享的原生分配和 coherent baseline
额度，与选择的 4 GiB guest 提交额度无关；原版内存 heap 查询却暴露宿主总容量。
现在额度随所选 guest 内存按八分之一计算，上限 1 GiB；4 GiB 设置对应 512 MiB，
同时限制报告的 heap 容量。额度不预分配，旧的每笔 64 MiB、跨 context 共享和
最终释放计费不变。[内存说明](../architecture/vulkan-compute.md#memory-and-lifetime)
区分 guest 页提交、原生额度及额外驱动开销，不将其称为统一的宿主 RSS 上限。

独立原版 Vulkan C peer 两轮各持有八笔 16 MiB coherent 映射，检查首尾写入、
释放再分配以及 512 MiB heap 上限。旧生产 FFI 在分配阶段返回内存不足；新生产
FFI 通过，独立宿主 Vulkan oracle 也通过。Vulkan 单元测试增至 29 项，通过。

带该额度的原版 Firefox/Mesa 已在 Linux CPU Vulkan 参考中得到实际 GPU 页面：
私有、临时的原生只读回读检查到完整 60,000 个绿色 canvas 像素。带原有内容沙箱
及默认 Wayland proxy，`gfx.webrender.all=true` 和 `layout.frame_rate=1` 均显式
记作诊断偏好。中文/emoji、方向键、Backspace/继续输入、两个输入框独占焦点和
800×600 调整均通过真实 DOM 回执；不是只看前端命令成功或窗口 revision。
该组关闭后窗口消失，但 Renderer 的 viewport 命令失败，退出码为 139；另一个
不重复输入/resize 的关闭组在 45 秒内没有退出。因此完整退出及默认刷新率尚未
验收，不能据这一诊断组发布新版或宣称物理 iPad 的 Firefox 已通过。

探针的 `--native-capture` 明确表示外部原生像素诊断，GPU handle 仍为不透明值；
它不会将普通 SHM、标题或旧 native texture 自动当作页面像素验收。`--close-only`
与 `--resize-before-close` 用于只重查失败的退出路径，复用已通过且相关输入源码
未变的输入检查。原生回读实现、截图、日志、地址和栈均仅留本地，不进入生产代码
或公开 Artifact；生产呈现继续保留原纹理，不加入像素回读。

## 默认刷新率与 Vulkan 边界

GPU 额度版本的 [CI 39628534758](https://github.com/kb5000/trames/actions/runs/39628534758)
已在 Linux 和云端 Apple Metal 通过原版 C 的 128 MiB 同时 coherent 映射、释放再
分配、Vulkan 29 项和 iOS FFI 编译。继续复用未变的 WSI/TF/局部更新像素结果。

默认刷新率下，只有降低无内容回调频率的对照仍未加载目标页面，未采用该单独
方案。进一步按实际产帧时间施加回调背压：快速生产者保持输出的 60 Hz，空提交
保留定时回调；慢生产者在新图像呈现后给普通任务留出生产间隔，连续中间图像
不延长已有截止时间，长时间空闲也不累积欠账。每个表面的调度使用自身的内容
revision，不能用其他表面的更新伪装成新帧。这不是修改 guest 时钟或线程调度。

此对照中，原版 Firefox/Mesa 在保留默认刷新率、内容沙箱及 Wayland proxy 时
已完成页面 JS，并通过实际 native 图像中的 60,000 个绿色 Canvas 像素；Linux
CPU Vulkan 参考仍显式设置 `gfx.webrender.all=true`，没有新增产品偏好。调整
800×600 时清除矩形校验仍失败，退出为 139。后续参数诊断确认它不是空矩形，
而是原版 Zink 的局部 render area 与调整大小时的 GL 清除区域不一致，确实违反
Vulkan 的 render-area 边界要求。EGL native-WSI 适配层暂不声明
`EGL_KHR_partial_update`，使其使用完整 render area；仍保留 damage swap 优化
提示、buffer age、原始 Gallium/Zink 和原生零拷贝呈现。不把越界命令放入宿主
驱动，也不将失败命令当作成功。该适配等待完整交互和退出验收。
输入源码未变的中文/emoji、方向键及焦点 DOM 验收继续引用前组，但新的刷新
调度仍需要完整交互和退出复核。所有截图、回读数据及地址日志仅留本地。

另修正两项过严的 Vulkan 检查：viewport 使用已缓存的原生设备尺寸和 bounds，
允许负原点及伸出 framebuffer 的 viewport 由原生裁剪；无需逐帧查询原生 limits。
`VkClearRect` 的 layerCount 必须大于零，但 extent 可以为零，空清除不应使程序
中止。render area、层范围、溢出和深度边界继续验证。新增标准 C 像素用例覆盖
静态/动态负 viewport 与空清除保留原有像素；Vulkan 30 项、Wayland 15 项和
受影响模块严格 Clippy 已通过。

源码 [2c8981b](https://github.com/kb5000/trames/commit/2c8981bd5d5fcaa944deb0e282df8db7b0230612)
的 [CI 39633612435](https://github.com/kb5000/trames/actions/runs/39633612435) 已在
Linux 和云端 Apple Metal 通过全部新增边界像素、原版 Mesa 双窗口 damage swap、
五种尺寸、关闭及旧帧保活，Vulkan 30 项、Wayland 15 项及 iOS FFI 编译通过。
Apple 的窗口纹理由现有 exported-MTLTexture oracle 逐像素检查；CPU reference
不冒充 Metal。公开合成证据 ZIP 已核对 SHA-256 和每项 CRC：Linux 6,988 bytes，
`bebf551f27f709dc2d6821abcf3fe82be48cbe9e24cc366e3ee58ddea0fdb8b6`；
Metal 34,981 bytes，`18d11cbe1df0baf1a7edb4b717cf4fec93f33dd06eb14042b52e77dc412f46c4`。

最终默认刷新率诊断还完成了中文/emoji、方向键/退格/继续输入、两个输入框往返
和 800×600 的 DOM/scene/native-frame 尺寸同步；不再触发 viewport 或清除矩形
校验错误。该组关闭后，Firefox 自己的 Shutdown Hang Terminator 因清理超时
主动访问空地址，退出 139；不是新的未知 CPU 指令或已修复的 Vulkan 验证错误。
另一个只重查关闭、跳过输入的组在 120 秒内未退出；其主线程在分配器、互斥锁
及嵌套事件/线程等待之间持续推进，未证明固定死锁。继续进行更长的有界退出
观察，保留 Firefox 默认看门狗设置，不能把窗口消失、主线程仍推进或其他组
输入通过当作正常退出。

扩大探针退出等待到 300 秒、关闭逐 Vulkan 调用/周期浏览器日志的同一运行时
对照完成正常退出 0：页面/native Canvas 通过后，只发送关闭，清理耗时约
191.39 秒。Firefox 默认刷新率、关闭看门狗、内容沙箱和 Wayland proxy 均保留；
这证明此前 120 秒未退出本身不能当作死锁。此组跳过输入/resize，尚不覆盖此前
完整输入组的超时失败；完整串联另行复测。未据此声称关闭性能已解决。

像素探针也区分没有新采集和真正的不匹配：诊断回读器在已验收绿色页面后可停止
记录，resize 后旧尺寸数据不可作为新尺寸的像素结果，但也不应把先前已验收页面
改记成从未通过。resize 继续要求实际 native-frame 尺寸及 DOM 回执。

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
- renderpass2 不再错误限制输出引用的 `aspectMask`。新的清除 fixture 使用
  带深度附件的 imageless renderpass2，并给颜色和深度输出设置有效但被忽略的
  aspect 位；独立宿主原版 Vulkan 与 ARM64 App FFI 的像素 oracle 均通过。

## 退出耗时与地址布局（2026-10-22）

默认刷新率 close-only 基线再次正常退出，但关闭耗时约 149 秒；完整输入和 resize
组仍曾触发 Firefox 原有 shutdown watchdog。原始日志和诊断数据保持私有。
对照原版 Scudo 释放路径，发现 unhinted 大块 `PROT_NONE` 预留也推进普通 mmap
游标：16 GiB 预留让后续小块映射跨过同样大的空洞，放大 allocator 区域扫描。

现在虚拟预留从独立地址区搜索，并复用释放的空洞，不占 resident RAM、不推进
普通堆映射游标。有效 hints 和 fixed 映射继续生效；`MREMAP_FIXED` 及预留移动
同样不能把普通分配游标搬走。没有修改 Firefox/Scudo 的回收算法、线程同步或
退出看门狗。新增回归在旧实现明确失败，修复后 Linux 29 项及严格 Clippy 通过；
原版 Firefox 的完整默认刷新率 GPU/输入/resize/退出复查进行中，尚不记作修复完成。
Linux 与 Apple ARM64 只验证受影响的 Linux state 路径和 iOS FFI，复用此前通过且
源码未变的 CPU/MMU/Wayland/Vulkan/Metal/guest/UI 检查。

地址布局源码 [d925bd5](https://github.com/kb5000/trames/commit/d925bd554fb5a70bcd0e29079e5d2535c322d17d)
的 [CI 39639878966](https://github.com/kb5000/trames/actions/runs/39639878966) 已通过
Linux/Apple ARM64 的 Linux state 20 项以及 iOS FFI 检查。Apple host granule 为
16 KiB。新 profile 的默认刷新率组仍持续重绘、停在标题回执前；采样已不再落在
Scudo 区域扫描，不能把消除该扫描说成完整 Firefox 验收。

同时发现硬 MMU 的 1 GiB 全局 VA 额度与多进程 4 GiB guest 不匹配：物理映射、
coordinator CPU view 和 fork/worker 别名共用此额度。私有对照实际有超过 10 GiB
的原生映射别名，与约 2–3 GiB resident RAM 不同。现将有界 VA 额度增至 32 GiB、
CPU 注册表增至 32,768 个 1 MiB 窗口，仍保留耗尽时的软件回退和原有权限、物理
所有权及 guest RAM 额度。新增只预留 CPU VA 的回归在旧实现失败；修复后 Memory
93 项和严格 Clippy 通过。此次只重查 Memory 和 iOS embedding，复用未变模块。
默认刷新率等待仍未解决；另用显式低刷新率诊断隔离退出路径，尚不据此宣称完整
默认设置的浏览器可用或构建已完成。

## 本轮退出结果与 1.3.0 发布（2026-10-22）

原版 Firefox 的显式 `layout.frame_rate=1` 隔离组已经完成 GPU 页面 60,000 绿色
像素、中文/emoji、方向键与退格后继续输入、两个输入框独占焦点往返、800×600
DOM/scene/native-frame 尺寸以及关闭。退出码为 0，关闭约 34.4 秒，完整诊断收集
成功；没有关闭或延长 Firefox 自身的退出看门狗。此配置用于隔离已知的刷新事件
拥塞，不能与此前默认刷新率 149–191 秒的 close-only 组计算严格加速比。

Memory 额度源码 [630afb2](https://github.com/kb5000/trames/commit/630afb260cfd48f05160bfdbc61bb9cd6a39bbf4)
的 [CI 39643715037](https://github.com/kb5000/trames/actions/runs/39643715037) 已完成
Linux/Apple ARM64 Memory 93 项、相关严格 Clippy 和 iOS FFI。未变的 Linux state
20 项、CPU/Wayland/Vulkan/Metal/guest 验收按源码复用。

同配置旧版对照也已完成：原版 Firefox/Mesa、同一离线页面、新 profile、4 CPU、
4 GiB、完整输入/resize、`gfx.webrender.all=true`、`layout.frame_rate=1` 及临时只读
native 像素检查。旧 2c8981b 构建关闭 98.4 秒后触发原有 watchdog，退出 139；
新 630afb2 构建关闭 34.4 秒正常退出 0。两组各一次，ASLR/调度仍有波动，而且旧
组没有正常完成，不能将比值当作稳定的正常退出加速比。它证明该失败场景已从
超时崩溃恢复到正常退出；仍不是默认刷新率或物理 iPad 的验收。

按项目所有者要求，直接发布现有已验证构建为 1.3.0 build 35，不追加重复验收。
生产源码不包含临时图像回读、线程栈
采集或浏览器偏好覆盖。默认刷新率的 CPU Vulkan 参考仍会持续重绘并延迟标题/
交互，完整 Firefox 支持及 [Issue #33](https://github.com/kb5000/trames/issues/33)
继续开放，不将隔离组说成默认设置已完整可用。

另已针对 IPA 实际附带的 musl 二进制完成一次 page/resize/close 对齐检查，
正常退出码 0，关闭约 39.2 秒；不重复已通过的完整输入项目。

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
基础清除版本 [f55e282](https://github.com/kb5000/trames/commit/f55e2829ca21998ccbb7784117b759ac3251c4da)
的 [Metal 定向 job](https://github.com/kb5000/trames/actions/runs/39614656207)
已完成同一清除像素检查、28 项 Vulkan 测试及 iOS FFI 检查。
最终输出引用修正 [9c1044c](https://github.com/kb5000/trames/commit/9c1044c1e7676dc23516c4fc547f0fcb26c34cdb)
的 [CI 39615564225](https://github.com/kb5000/trames/actions/runs/39615564225)
中 Linux 与 Apple 定向 job 均已完成相应检查与新 fixture；其中公开 Linux 证据
ZIP（6,115 bytes）的 SHA-256 为
`1ca84c647ea25831bb6ac79a3e2c6c11de548e3ffdfba95fd08aeae2d26c29a1`，
已逐项 CRC 检查并核实 GPU 清除通过。Metal 证据 ZIP（23,544 bytes）的 SHA-256
为 `47474ef7f664e040d2990d972dd413cdb01c1ef72c4c012834b2ed714e2d217d`，
同样通过全部 CRC 与清除结果检查，包含绑定到 9c1044c 的 464 项 guest 输入清单。
两端使用相同的 guest ICD 哈希，最终 imageless/depth/忽略输出 aspect 的 fixture
已在云端 Metal 通过；iOS FFI 检查也通过。物理 iPad 仍待验收。

复查入口：先用 `prepare-zink-guest.py --wayland` 准备原版签名包和客户端，
再运行 `tests/integration/zink-wayland.py --relay --output <新目录>`。
`--clear-only` 仅检查新增的 render-pass 局部清除；其余通过结果需独立引用。
`--in-place-store --store <临时 store>` 可避免复制 rootfs，但不得同时运行其他
使用该 store 的 session。Apple 还要求已有的 exported-MTLTexture 像素 oracle。

Firefox 的 `--require-gpu-frame` 只适用于 GPU 开启的原生 Wayland 窗口，要求
带目标 JS 标题的同一窗口取得 native GPU 图像；拒绝 SHM 捕获模式或 Xwayland，
避免把软件图像或 Xwayland glamor 当作浏览器 GPU 通过。它只验证图像传输，
仍不能替代最终页面 canvas 像素检查。非法参数在打开 session 前拒绝。

Firefox 原始日志、截图、地址、线程栈及运行转储仅保留本地；临时故障采集不进入
生产 Core。[Issue #33](https://github.com/kb5000/trames/issues/33)保持开放。
1.3.0 的构建、IPA 及对应源码清单见 [发布记录](ipad-1.3-build35.json)。
