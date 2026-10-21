# 原生 depth/stencil 路径

2026-10-21。继续原版 Mesa/Zink → Vulkan → MoltenVK 路线，公共入口仍为188个。
不修改Mesa/Firefox版本、扩展或GLSL，不使用GL版本覆盖；未构建IPA。

## 实现

- 查询/创建D16、X8D24、D32、S8、D16S8、D24S8、D32S8，受原版driver的实际format
  properties与image-format query约束。不把枚举存在视为宿主支持。
- packed图片可创建depth、stencil或组合view；采样descriptor要求单一aspect。
  image barrier同时转换packed图片的两种aspect，未宣告separateDepthStencilLayouts。
- buffer/image复制只接受一个aspect，按其实际字节数验证完整row/layer/mip范围；
  例如D32S8的depth为4字节、stencil为1字节，而非两者均8字节。image copy/blit
  保留匹配的aspect与depth/stencil格式约束，后者只允许nearest及真实native blit能力。
- 独立depth/stencil clear；legacy/renderpass2及bound/imageless保留各自load/store、
  clear值和真实格式。没有修改资源保留/原生提交的生命周期。
- front/back独立fail/pass/depth-fail、全部8种stencil op/compare与静态/动态
  compare-mask/write-mask/reference，由原生pipeline/command执行。没有CPU模板模拟、
  整次绘制锁或未实现调用的空成功返回。
- 旧wire默认禁用stencil；新pipeline config以bit1024标记固定15个u64尾部，位于
  `23+8*8`个u64后：enable、front/back各7项。renderpass以a[12]=1标记7项
  depth/stencil descriptor，旧5项保持stencil DONT_CARE。其他wire仍兼容revision8。

## 验证

同一普通Vulkan C客户端分别经过原版宿主loader与guest loader/ICD；SPIR-V由原版
glslang生成。独立标量公式计算预期模板/深度/颜色，不从另一渲染器复制预期。

- Linux原生/guest均通过6种宿主支持格式的buffer→image→image→buffer，涵盖非零mip、
  两层、独立depth/stencil、带padding的行/层、精确末尾边界；D16S8由原生查询拒绝，
  未宣称支持。X8_D24未定义的高8位不参与比较。
- 6轮共18,432像素覆盖初始0/160/255、8种op/compare、front/back、stencil-fail、
  depth-fail、静态/动态独立mask/ref、clamp/wrap边界，以及legacy/bound和
  renderpass2/imageless组合。逐像素验证stencil、depth和RGBA。
- 独立清除depth保留stencil，清除stencil保留depth；原生fragment shader
  从depth-only shadow view及stencil-only unsigned view采样，3,072个RGBA像素正确。
- Vulkan库26项测试、严格Release Clippy全targets、严格C编译、Rust格式、
  Python语法与workflow YAML检查通过。仅验证受影响的Vulkan路径；CPU、Linux、
  网络、UI与未改动的query/TF数据验收复用前批结果。

本地Linux使用原版Lavapipe CPU Vulkan参考，不代表Apple GPU性能或iPad验收。
Apple Metal与iOS FFI编译也已通过，详见下方同源CI记录。

```sh
python3 tests/integration/vulkan-stencil.py --store target/zink-inputs/store \
  --output target/vulkan-stencil-new --native-client target/zink-inputs/vulkan-stencil-host
python3 tests/integration/zink-es3.py --store target/zink-inputs/store \
  --output target/zink-caps-new --stage capabilities
python3 tests/integration/zink-es3.py --store target/zink-inputs/store \
  --output target/zink-es3-new --stage es3 --capability-probe
```

`--capability-probe`仅允许明确的 `GLES3 configs=0` 作为可记录的“不可用”结果；
JSON中的 `passed` 保持false。任何其他错误仍失败，默认ES3验收没有放宽。

## 剩余前提

原版Mesa25.1.9现在报告 `GL_ARB_depth_buffer_float=1`，标准OpenGL扩展探针明确
显示 `GL_ARB_instanced_arrays=0`。原版Zink通过 `VK_EXT_vertex_attribute_divisor`
决定该cap，不能仅靠已有的per-instance divisor=1输入宣告完整支持。
ES3 configs仍为0；下一项是原生divisor feature/property、pipeline pNext、
实际输入索引与GPU数据测试。Apple另需实现原版MoltenVK缺少的原生GPU TF。

完整GLES3、Firefox硬件WebRender和iPad实际图形体验均未据此宣称完成。


## 同源 CI 完成

源码 [3703467](https://github.com/kb5000/trames/commit/3703467031bcf7362636e644e899ed82a2518e66)
的 [CI 39574168076](https://github.com/kb5000/trames/actions/runs/39574168076) 全部通过。
Linux原生/guest与Apple Paravirtual GPU的guest/原版loader均完成上述模板、深度和采样读回。
Apple实际支持D16、D32、S8、D32S8四种传输格式；X8D24、D16S8、D24S8被真实
查询拒绝，没有强行暴露。两边各验证18,432个模板/深度/颜色像素和3,072个采样像素。

两边26项Vulkan测试、严格Clippy、受影响的原生附件/子资源回归及Apple iOS FFI
检查通过。Mac/iOS固定原版MoltenVK源码、两个patch和构建recipe均未变，库缓存的
input hashes与library SHA再次核对一致，未无谓重编原生依赖。
Linux/Metal两个ZIP的SHA-256/全部CRC、guest client/ICD/三份SPIR-V的跨宿主同一性、
source-bound bundle和native build metadata均已校验，见[证据摘要](vulkan-stencil-ci.json)。

原版Mesa在Linux/Metal均报告 `ARB_depth_buffer_float=1` 和 `ARB_instanced_arrays=0`；
Linux的TF=1、Metal的TF=0。两边的ES3 configs均为0，JSON明确保留 `passed=false`。
仅追加证据不重复上述验收；没有iPad真机验收或IPA，也未公开Firefox原始数据。
