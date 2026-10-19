# Trames 1.2.0 / build 33

应用源码`dbd0f88`，[iPad构建39269763734](https://github.com/kb5000/trames/actions/runs/39269763734)成功。
实际IPA为iPhoneOS ARM64、最低iPadOS17，主程序和File Provider扩展均为1.2.0/build33，
共享App Group一致，无代码签名、描述文件或模拟器二进制。包大小11,159,672字节，
SHA-256为`c46f6a49f7ee66f8b6397b2b2530edff4062ad71b2f9dde183dd0c9013f47bb1`。

本版重编译当前Release宿主；只改Cargo和Xcode版本元数据，依赖图不变。运行算法/源码
保持已通过的18162c9；[图形CI39264897300](https://github.com/kb5000/trames/actions/runs/39264897300)
的Linux/Apple原生Vulkan模块、GLES2双窗口、resize/close/重启、全部导出Metal纹理像素与
旧帧保活结果复用。UI源码和项目版本字段外配置与39062598012相同，复用其模拟器交互；
musl算法/补丁/构建输入与39015795319相同，连同对应源码复用。源码和CI步骤逐项核验后
只运行Swift解析/typecheck、组件绑定、iOS优化编译与包校验，未重跑无关的通过项目。

IPA中的ICD和EGL逐字节摘要等于Metal验收版本：

| 组件 | SHA-256 |
| --- | --- |
| guest ICD | `6c737c307cc3c3bf07d257b1cb758a92a3de8cad8bf008039caf9f9a8b233e9d` |
| Mesa EGL | `4ae608c00641d154e68cb6dfb1bf890613ebb22ec6edf1b5e8b2c02fe6b179b8` |
| hybrid musl | `885b308b75e36bb402b311fba1d3f1284e6dd8d3a0bfbf88686bff1b629f9b23` |

原版Gallium/Zink/GLES2仍由用户从签名APK安装；只对EGL做显式无DRM平台适配。
正常窗口呈现无CPU像素上传，oracle读回单独用于验证。资源中保留Mesa EGL补丁与许可证，
对应musl源包逐输入摘要核验，原版MoltenVK源码闭包按固定上游摘要核验，Mesa25.1.9源归档
按原始摘要核验。发布脚本还核对IPA每个文件与Release device app一致，并下载发布资产
复核摘要；不覆盖历史Release/tag。实际检查字段见[结构化记录](release-1.2.json)。

这次没有运行物理iPad；CI的Paravirtual Metal结果不作为iPad性能或全部GL一致性认证。
更多GL版本/格式/渲染状态与既有Docker、File Provider限制见[发行说明](release-1.2-notes.md)。
