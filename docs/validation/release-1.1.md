# Trames 1.1.0 / build32

[正式Release](https://github.com/kb5000/trames/releases/tag/v1.1.0) ·
[unsigned IPA](https://github.com/kb5000/trames/releases/download/v1.1.0/Trames-1.1.0-build32-unsigned.ipa) ·
[发行说明及范围](release-1.1-notes.md) · [全部源码/验证指纹](release-1.1.json)。

PR #30合并默认runc/overlay2、guest网络隔离、内存/proc/route与交互PTY修复。
当前应用源码d1d0185；iPad版本/构建号1.1.0/32，ARM64 iPhoneOS unsigned，
主App/extension与共享组匹配。下载ZIP/IPA CRC/SHA256、guest ELF/hash、debug样例
排除和许可证标记已检查。实际iPad复测未冒充CI结果。

发布总检查：
- Linux格式/严格Clippy/477项Rust测试通过，原[39147131597](https://github.com/kb5000/trames/actions/runs/39147131597)
  在后续iOS cross check因缺少llvm-ar失败。补齐工具的[39147626609](https://github.com/kb5000/trames/actions/runs/39147626609)
  只重试该项并通过，生产源码与原测试源完全相同；不把首次整个workflow写成通过。
- Apple [39147131590](https://github.com/kb5000/trames/actions/runs/39147131590) 478项Rust测试、
  当前aarch64-apple-ios release优化编译/Xcode Release链接通过。
- 原版Docker run/exec/退出/overlay重启、网络、4GiB/8CPU MMU启动、PTY输入往返
  保留各自范围的已有证据；UI/CPU差分/GPU/guest库源码不变，不重复验收。
- 出版脚本在合并后再次验证同源CI、IPA全部文件对应Release设备构建、GPLv3及原始
  musl/MoltenVK源码，上传checksum/verification/项目和对应库源码，最后公开Release并回读资产哈希。

IPA SHA256：`e6c4c5da9aee9f10fcc65dfb173b9593a2cf3858ad1447b66e71ce091acc3f94`。

后续范围由#29（Docker）、#24（code-server）、#23（解释器性能）、#18（实验native allocator）
与#8（设备/FileProvider）继续跟踪。关闭旧功能或重复Issue不代表未实现功能已经完成。

发布已完成：v1.1.0固定在b25feb43，公开发行工作流39147982945成功，6项资产上传并
逐个回读核验。另行从公开Release URL下载IPA，SHA256与已检查Actions IPA一致。
项目/库对应源码、checksum与verification JSON均已公开；未替换任何旧tag/资产。
本文件后续记录提交只更新文档，不移动正式tag。

Issue整理：#19/#21的初始功能交付完成；#22的后续事项转入#29后关闭；#9因原生
SSH/SFTP退出当前App集成范围按not_planned关闭，不宣称NIOSSH崩溃已修复。
#8/#18/#23/#24/#29保留，分别跟踪真机/FileProvider、实验allocator、性能、code-server和Docker剩余项。
