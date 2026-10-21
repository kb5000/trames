# 原生实例输入 divisor 与首个 GLES3 上下文

2026-10-21。继续原版 Mesa/Zink → Vulkan → MoltenVK；公共入口仍为188个，
新增 `VK_EXT_vertex_attribute_divisor` 的真实feature/property与pipeline pNext传输。
直接复用原版driver的顶点提取/Metal step-function，不重写实例算法，没有CPU展开VBO
或将TF/实例化操作做成空返回。原版MoltenVK源码与此前两个patch均未改动。

## 能力与状态

- 仅当native EXT及rate-divisor feature实际可用时枚举扩展，specVersion=3；
  maximum与zero-divisor按真实query返回。扩展启用与两个feature分别校验。
- pipeline支持稀疏/乱序binding的独立divisor，拒绝重复、不存在和非INSTANCE binding。
  guest的pNext/指针不进入native compiler；复制后的native数组保留到异步编译结束。
- 旧wire binding仍为3个u64、默认divisor=1；config bit2048标记4个u64的
  `[binding,stride,inputRate,divisor]`。物理query op150写入4字节maximum；revision8兼容。
- draw的实例范围按实际divisor计算；零divisor固定元素0，divisor1保留直接加法快路，
  其他值计算末次元素。紧凑的共享实例VBO不再按全部instance数量误判越界。
- 当前只暴露EXT，不宣告KHR的supportsNonZeroFirstInstance。任一INSTANCE binding
  的divisor不为1时要求firstInstance=0，包含未使用binding和byte-count draw；
  divisor1支持非零firstInstance。索引/计数留在GPU，不进行CPU回读/扫描。
- 不新增全局锁、访存权限策略或解释器路径开销。默认firstInstance=0不扫描额外列表。

## 数据验证

同一普通Vulkan C客户端经原版宿主loader和ARM64 guest/原版loader运行：
18次绘制、9,216个精确RGBA像素，覆盖0/1/2/3/native最大divisor、两种独立实例输入、
稀疏binding、恰好足够的buffer、firstInstance、direct/16-bit/32-bit indexed及负baseVertex。
zero-divisor仅在真实feature支持时测试；该本地reference支持。负例验证缺少扩展、
重复/不存在/非INSTANCE binding和不合法firstInstance均在桥接处失败。

原版签名Mesa25.1.9的标准OpenGL context执行divisor2/3、四个实例、紧凑color VBO，
128个像素正确。注意GL的divisor0表示per-vertex，与Vulkan的零divisor固定元素不同，
不能用不足6个顶点的color VBO测试GL divisor0。Vulkan零divisor由上面的直接客户端验证。

原版Mesa首次返回 `EGL GLES3 configs=1`、`OpenGL ES 3.0 Mesa 25.1.9`，并完成
实际GLSL MRT的512个像素及12个transform-feedback标量验证，无版本/扩展覆盖。
Linux使用Lavapipe CPU Vulkan参考，不能替代Apple GPU或性能验收。

27项Vulkan库测试、严格Release Clippy全targets、严格C编译、格式、Python、
workflow YAML及shell语法通过。CI仅检查受影响的实例输入、原版Mesa新路径及iOS FFI；
之前已通过且未改动的image/stencil、独立query、CPU/Linux/network/UI验收复用。

```sh
python3 tests/integration/vulkan-divisor.py --store target/zink-inputs/store \
  --output target/vulkan-divisor-new --native-client target/zink-inputs/vulkan-divisor-host
python3 tests/integration/zink-es3.py --store target/zink-inputs/store \
  --output target/zink-instancing-new --stage instancing
python3 tests/integration/zink-es3.py --store target/zink-inputs/store \
  --output target/zink-gles3-new --stage es3
```

## 边界

Apple Metal数据与iOS编译检查由本批CI验证，完成后追加。Apple仍缺原生GPU TF，
不把Linux的GLES3结果当作iPad已支持。Mesa仍对缺少custom-border-color、
line-rasterization及depth-clip-enable给出警告；本探针的几何处于clip范围内。
完整GLES3/Vulkan conformance、其他渲染状态、Firefox GPU首帧/WSI交接及iPad体验
继续单独验收；没有构建IPA或公开Firefox原始诊断。
