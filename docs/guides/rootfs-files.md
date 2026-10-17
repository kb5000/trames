# 在文件 App 中浏览 rootfs

0.18.0 build 22 增加独立 File Provider 扩展。文件 App 的「浏览」中启用 Trames
位置后，依次打开 **Trames → Alpine**，即可看到该实例的持久化根目录。
若位置尚未显示，可在「浏览 → … → 编辑」中启用。

本版支持目录浏览、文件预览及把普通文件复制／导出到其他位置，均为只读。
Linux 符号链接与特殊设备仅显示元数据，不在宿主文件系统中跟随链接；运行时
`/proc`、`/sys` 等虚拟端点也不作为可导出的磁盘文件。修改、删除、移动及新文件
导入仍使用 App 的 Files 面板或 guest 命令。导入现在可选择原子替换同名文件；
传输中断时保留旧文件，导出不覆盖已有宿主文件。

扩展读取现有原子 manifest 与 inode 对象，不启动第二个 Linux/VFS 写入实例，
也不运行存储恢复。App 暂停或退出后，已经落盘的数据仍可浏览。复制固定到打开
的 inode；若 guest 同时删除或缩短文件，操作可报告不可用并重试，不会改动 rootfs。
文件内容按 64 KiB 分块复制到系统管理的 provider 缓存。

`roots.json` 使用独立的实例标识、显示名称和相对 store 路径；当前注册 Alpine，
以后增加其他 rootfs 无需改变文件 App 的层级或把实例名硬编码到条目标识中。
共享容器可用时，App 在首次启动、VM 尚未打开前保留并迁移现有 environment；
Files 的目录授权记录仍在主 App 私有目录中。

File Provider 需要主 App 与扩展具有相同的有效 **App Group** 签名权限。
`TRAMES_APP_GROUP` 是构建配置，默认 `group.org.trames.Trames`。安装工具必须为两者
保留或正确映射相同的权限及扩展 Info.plist 中的 document group。普通未签名 IPA
本身不授予该权限；Sideloadly 的免费签名是否能提供该权限须在实际安装后确认。
不具备共享容器时，App 使用原有私有 store，Files 面板会说明当前不可用，已有
Linux 数据和手动导入／导出继续保留。扩展编译／模拟器验证不替代这项真机签名验收。

平台接口依据：[Apple 的 Document Provider 说明](https://developer.apple.com/library/archive/documentation/General/Conceptual/ExtensibilityPG/FileProvider.html)。
