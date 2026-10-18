# iPad 1.0.0 build31：Docker交互式TTY

[下载unsigned IPA压缩包](https://github.com/kb5000/trames/actions/runs/39143544853/artifacts/12142766562)，解压后用Windows Sideloadly签名安装。
包含build30启动挂载和此前MMU/route修复，以及本轮devpts inode/proc设备重开修复。

[原版Docker TTY回归及边界](devpts-inode.md)：默认runc/overlay2的run -it --rm、
终端输入/输出往返、退出和Docker API删除成功。Linux原生对照、4项PTY及11项proc
相关回归（1项重叠）、严格VFS Clippy通过。Apple相同4+11项及当前ARM64 Release应用
[构建通过](https://github.com/kb5000/trames/actions/runs/39142856325)。

包检查发现第一份新代码IPA的构建号仍为30；最终[元数据重打包](https://github.com/kb5000/trames/actions/runs/39143544853)
修正主App和FileProvider的CFBundleVersion为31。没有重复功能测试或编译；除Info.plist外
所有bundle文件（包含两个Mach-O、musl、Vulkan）与已验证Release应用SHA256完全相同。
最终主App/extension均1.0.0/31、iPhoneOS arm64、unsigned、App Group一致；
ZIP及IPA CRC/SHA256核验通过。无调试Wayland样例和原生CoreMark。实际iPad执行待用户复测。

IPA SHA256：`15b1ddd7a92806170f78ba57c2e7e889b87cfc1214500f07bd2c20134ad49673`。
完整[版本、源码、证据和包指纹](ipad-1.0-build31.json)。保留GPLv3及上游授权和源码闭包；
guest源码产物沿用已记录39015795319，runtime源码c9292ccc、元数据/打包配置0d13d8be。
未合并main、修改正式tag或创建Release。
