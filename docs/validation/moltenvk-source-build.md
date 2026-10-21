# MoltenVK 1.4.2：从原版源码构建最小修正

2026-10-21，图像/mipmap 的 Metal 验收找出上游 `MVKCmdBlitImage::encode` 的
3D 坐标错误：对源图像查询深度时用了 `dstSubresource.mipLevel`。4→2 的 mip
缩小将第一个目的层从源 z=2 取值，正确位置应为 z=1；Linux 原生 Vulkan
读回与完整缓冲区比较共同定位了差异。

[补丁](../../patches/moltenvk/0001-use-source-mip-depth-for-volume-blits.patch)
只把该处索引改成 `srcSubresource.mipLevel`。保留 MoltenVK 原有的 GPU blit、
插值、格式转换、同步与资源算法，不修改 guest 坐标或降低测试预期。

原版来源固定为 `db66022459ffb663aa2b50f6b018bc2e124f5edf`，源码归档 SHA-256
`c6ced1eb31827be7f10e2d8d77087486c75e595ab068e665a712d73b99a8ff06`。
原始 `MVKCmdTransfer.mm` SHA-256 为
`7c050a2724cc75170b789557d826ac3614e08bd129b00b1dcf49a0a375e75e70`，
修正后为 `2e902bfacf12526263801bd4661c286b3d2ed7ddb7ea79d660c20af892d641ed`。

```sh
# 有 Xcode 的 Mac；使用已固定并保留完整对应源码的依赖。
python3 scripts/prepare-vulkan.py --apple --source-build
export TRAMES_MOLTENVK_STATIC_DIR="$(python3 scripts/build-moltenvk.py --platform macos --arch arm64)"
# 也可选择 ios 或 ios-simulator；不需要签名账号或打包 App。
python3 scripts/build-moltenvk.py --platform ios --arch arm64
```

脚本先校验原始归档摘要，复制独立的 MoltenVK source tree，并链接固定依赖源码，
复用上游 fetchDependencies 同样的固定 SPIRV-Headers 与原包预生成 SPIRV-Tools
头文件，再应用并逐项核对最小补丁的准确差异。它调用上游原有 ExternalDependencies scheme
及 MoltenVK static target，Release 构建所选 SDK/架构；不更改上游二进制归档，
不在已链接的 master object 中热替换符号，也不静默退回旧驱动。

缓存键包括原版 manifest、补丁、构建脚本、平台/架构、Xcode 与 SDK；缓存
`libMoltenVK.a` 必须匹配记录中的 SHA-256。`build.json` 记录实际命令、工具链、
源文件与产物摘要，`xcodebuild.log` 保留完整编译输出。iPad 构建脚本自动选用该
原生库，标准 guest ICD 与原版 Mesa 保持不变。

对应源码包保留所有原版依赖归档、许可证、最小 patch 和构建脚本。重建时将
`upstream/` 中归档放入仓库 `target/vulkan-upstream/`，执行 prepare 命令以校验、
解压，再执行上述构建。MoltenVK 保留原 Apache-2.0 通知，Trames 为 GPLv3。

Linux 上已用固定源码归档验证 patch 精确应用、仅一行差异及修正后的摘要。
[CI 39551642471](https://github.com/kb5000/trames/actions/runs/39551642471) 已通过：
实际 Metal 3D mip、3D slice attachment、原版 Mesa MRT/mip/cube 像素正确；
arm64 iOS 原生库也成功编译。Xcode 16.4，macOS SDK 15.5 / iOS SDK 18.5，
原生库与输入 hash 见[构建记录](zink-images-ci.json)，物理 iPad 尚待验收。
先前已经通过的同源 Linux
GPU/Mesa、Apple module/23 tests/iOS FFI 检查不重跑；CI 严格核对 guest/backend/
fixture/toolchain 字节未变和已通过记录，再复用同源 guest bundle。

## 时间戳能力门控

后续 query 验收发现 Apple Paravirtual GPU 没有 timestamp counter set：原版
MoltenVK 在完成回调中生成 CPU 时间戳，CPU results 非零，但此前编码的 GPU copy
得到 0。[第二个补丁](../../patches/moltenvk/0002-require-native-gpu-timestamp-counters.patch)
只把 queue `timestampValidBits` 和 `timestampComputeAndGraphics` 改为基于实际
Metal timestamp counter set；没有该硬件路径时不声明时间戳支持，保留 occlusion。
有硬件计数器时保留原生 query 实现，不以 CPU 值冒充 GPU query copy。

两个补丁都针对同一固定原版归档验证后应用；原始 `MVKDevice.mm` SHA-256 为
`e93c8fe096d9c9f8c0b02da6443bc820dc42ca4340b122cc8dd11721ad7e9b8d`，
修改后为 `f5bb7a347f292e3a1ff5a2b1c312bf544336a406f0f168b399cd4d1fbb29e56a`。
新 patch 的摘要也进入原生缓存键和 `build.json`，不会复用未修正的库。
