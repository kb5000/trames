# 工作区窗口与 Wayland 呈现回归

跟踪 [Issue #27](https://github.com/kb5000/trames/issues/27)。用户在正式版
0.17.0/build19 的 iPad 上报告，终端浮动后最大化会卡死、最小化入口不符合
预期、画布越界和 vkmark 提交帧率与观看流畅程度不一致。

## 修复内容

- 每个工作区项目在同一个 ForEach 身份下保留唯一的终端/Metal 视图。
  切换标签页、浮动和停靠只改变几何布局与可见性，不把同一个
  TerminalViewport 交给两个 UIViewRepresentable 宿主。终端保留 PTY、
  滚动历史和解释器状态；原生键盘焦点切换增加重入保护。
- 浮动窗口最小化后成为顶部的非活动标签页，点击标签即可激活。
  标签页上下文菜单没有最小化项，+ 菜单不再收纳隐藏窗口。
  选中其他标签页即进入后台；不为后台图形标签提交 Metal 帧。
- 客户区域是画布的唯一尺寸来源。旧 buffer 的 scene bounds 不再决定
  UIView 大小；绘制裁在客户区域内，包含客户端装饰的负坐标。
  窗口外的 scene 内容也受该区域裁剪。渲染和输入使用同一逻辑原点。
  resize 以实际客户区域大小合并请求，最终请求不会丢失；模式变化与
  标签页恢复也重新发送尺寸，避免后到的通用最大化 configure 覆盖结果。
- Wayland 图像 revision 直接供 CADisplayLink 消费。仅布局、标题、模式、
  文本上下文变化发布到 SwiftUI，不因每帧图像更新重建工作区。
  CAMetalLayer 只有尺寸实际变化时才重置 drawable 与图像 revision。
- GPU fence 尚未就绪时，合成器重试周期由 16ms 改为 1ms。空闲或等待
  FIFO 呈现确认时保留事件驱动的等待，不把后台合成器变成忙轮询。
- GPU command 完成与屏幕呈现分开：通过 drawable presented handler
  才发送 Wayland presented/frame callback，失败命令单独回收。
  最多保留两个呈现中的 UI 帧；隐藏、离开 UIWindow 或从后台恢复时，
  未显示帧在 GPU 读操作完成后回收，不保留永远等待的呈现许可。
  模拟器 SDK 没有 drawable presented handler/presentedTime，协议通过
  GPU 完成回调推进；completed 与 presented 单独计数，后者在模拟器为
  unavailable，不能据此声称真实显示或延迟验证。
  继续保留原有 SHM bytesNoCopy 与
  MoltenVK Metal texture 导入，不引入生产路径像素回读。

## 验证

本轮执行器离线，未声称运行本地测试。Apple CI 验证以下内容：

1. Swift 语法检查和 Foundation 工作区状态测试。
2. UIKit 宿主身份：连续浮动、停靠与切后台保持同一个终端父视图。
3. 实际 UIKit 画布尺寸、连续缩小后的最终 resize 请求、停靠后视图
   身份、后台停止与前台恢复呈现。
4. 重复图像 revision 不发布布局，真正改变宽度时发布。
5. UI 手势覆盖最小化回到顶部、标签激活、浮动和停靠。
6. 在真实运行 Alpine 的 PTY 上重复终端浮动/最大化，再执行 bridge
   命令验证 App 与 guest 继续响应。
7. 原始 Wayland 客户端、Weston 输入、vkcube 与 vkmark 实际 Metal
   drawable 像素与 GPU 完成，包括 vkcube 运行期间隐藏/恢复再完成其余帧；
   现有 GPU producer readiness/FIFO/Mailbox 生命周期测试。

画布 accessibilityValue 区分 submitted、completed 与 presented，并记录真机观测到的
最大显示间隔 maxIntervalMs；原始客户端验收日志逐场景保存提交数和
完成数及呈现反馈来源。静止、遮挡、后台和场景切换也可能增加最大间隔，因此它
不是独立的动画卡顿判定。模拟器不用于宣称 iPad 60/120Hz 稳定帧率，
最终流畅度仍需真机复测。
