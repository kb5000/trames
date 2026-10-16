// SPDX-License-Identifier: GPL-3.0-or-later
import SwiftUI
import TramesKit
@main
struct TramesApp: App {
    @StateObject private var workspace = WorkspaceModel()
    @Environment(\.scenePhase) private var scenePhase
    var body: some Scene {
        WindowGroup {
            WorkspaceView(workspace: workspace)
                .preferredColorScheme(.dark)
                .onChange(of: scenePhase) { _, phase in
                    if phase == .background { workspace.background() }
                    else if phase == .active { workspace.foreground() }
                }
        }
    }
}
struct WorkspaceView: View {
    @ObservedObject var workspace: WorkspaceModel
    @State private var sshSheet = false
    @State private var filesSheet = false
    @State private var settingsSheet = false
    @State private var sessionActions = false
    @State private var coreMarkSheet = false
    @StateObject private var coreMark = NativeCoreMarkRunner()
    @Environment(\.scenePhase) private var scenePhase
    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Text("TRAMES").font(.system(.headline, design: .monospaced)).foregroundStyle(.mint)
                if workspace.sessions.isEmpty {
                    Spacer(minLength: 8)
                } else {
                    ScrollView(.horizontal, showsIndicators: false) {
                        HStack {
                            ForEach(workspace.sessions) { session in
                                Button { workspace.selected = session.id } label: {
                                    Text(session.title).lineLimit(1).padding(8)
                                        .background(workspace.selected == session.id ? Color.white.opacity(0.12) : .clear).clipShape(RoundedRectangle(cornerRadius: 6))
                                }.contextMenu { Button("关闭终端", role: .destructive) { workspace.close(session) } }
                            }
                        }
                    }
                }
                Button { sessionActions = true } label: {
                    Label("新建或管理会话", systemImage: "plus")
                        .labelStyle(.iconOnly).frame(minWidth: 44, minHeight: 44)
                }.confirmationDialog("新建或管理会话", isPresented: $sessionActions, titleVisibility: .visible) {
                    Button("本地 Alpine", systemImage: "terminal") { workspace.openLocal() }
                    Button("SSH 连接", systemImage: "network") { sshSheet = true }
                    Button("Files / SFTP", systemImage: "folder") { filesSheet = true }
                    if let local = workspace.local, local.displayEnabled {
                        Button("显示窗口", systemImage: "rectangle.on.rectangle") { local.displayVisible = true }
                    }
                    Button("设置", systemImage: "gear") { settingsSheet = true }
                    Button("原生 CoreMark", systemImage: "speedometer") { coreMarkSheet = true }
                }
                if let current = workspace.current { Button { workspace.close(current) } label: {
                    Label("关闭当前终端", systemImage: "xmark")
                        .labelStyle(.iconOnly).frame(minWidth: 44, minHeight: 44)
                } }
            }.padding(.horizontal, 12).padding(.vertical, 6)
            if let current = workspace.current {
                SessionView(session: current).id(current.id)
            } else {
                ContentUnavailableView("打开一个终端", systemImage: "terminal", description: Text("从右上角添加本地 Alpine 或 SSH 会话。"))
            }
        }
        .background(.black)
        .sheet(isPresented: $sshSheet) { SSHConnectView(workspace: workspace) }
        .sheet(isPresented: $filesSheet) { if let session = workspace.current { TransferView(session: session) } }
        .sheet(isPresented: $settingsSheet) { SettingsView(workspace: workspace) }
        .sheet(isPresented: $coreMarkSheet) { NativeCoreMarkView(runner: coreMark, workspace: workspace) }
        .onChange(of: scenePhase) { _, phase in
            if phase != .active { coreMark.markInterrupted() }
        }
        .overlay(alignment: .topLeading) {
            GeometryReader { proxy in
                if let local = workspace.local {
                    VulkanDisplayWindow(session: local, viewport: proxy.size).id(local.id)
                }
            }
        }
        .alert("验证 SSH 主机指纹", isPresented: Binding(get: { workspace.challenge != nil }, set: { if !$0 { workspace.challenge = nil } })) {
            Button("信任此密钥") { workspace.trust() }
            Button("取消", role: .cancel) { workspace.challenge = nil }
        } message: { Text("\(workspace.challenge?.endpoint ?? "")\n\(workspace.challenge?.fingerprint ?? "")\n请与服务器管理员提供的指纹核对。信任后重新发起连接。") }
        .alert("Trames", isPresented: Binding(get: { workspace.error != nil }, set: { if !$0 { workspace.error = nil } })) {
            Button("好") { workspace.error = nil }
        } message: { Text(workspace.error ?? "") }
    }
}
struct SessionView: View {
    @ObservedObject var session: TerminalSession
    @AppStorage("extraKeys") private var keys = "Esc,Tab,Ctrl-C,Ctrl-D,↑,↓,←,→"
    private let bytes: [String:[UInt8]] = ["Esc":[27], "Tab":[9], "Ctrl-C":[3], "Ctrl-D":[4], "↑":[27,91,65], "↓":[27,91,66], "←":[27,91,68], "→":[27,91,67]]
    var body: some View {
        VStack(spacing: 0) {
            TerminalPane(session: session)
            ScrollView(.horizontal, showsIndicators: false) {
                HStack {
                    ForEach(Array(keys.split(separator: ",").enumerated()), id: \.offset) { _, key in
                        Button(String(key)) { session.send(Data(bytes[String(key)] ?? Array(key.utf8))) }
                            .font(.system(.callout, design: .monospaced)).padding(.horizontal, 13).padding(.vertical, 8)
                            .background(Color.white.opacity(0.08)).clipShape(RoundedRectangle(cornerRadius: 5))
                    }
                    Button { session.terminal.becomeFirstResponder() } label: { Image(systemName: "keyboard") }
                        .accessibilityLabel("显示终端键盘").accessibilityIdentifier("terminal-keyboard")
                }.padding(6)
            }
            HStack {
                Text(session.state).lineLimit(2).accessibilityIdentifier("session-state")
                Spacer()
                if let capacity = session.capacity {
                    Text("\(ByteCountFormatter.string(fromByteCount: Int64(capacity.used_bytes), countStyle: .binary)) / \(ByteCountFormatter.string(fromByteCount: Int64(capacity.budget_bytes), countStyle: .binary))")
                }
            }.font(.caption).foregroundStyle(.secondary).padding(.horizontal, 12).padding(.vertical, 6)
        }.background(BridgeConsentPresenter(controller: session.bridge))
    }
}
struct SettingsView: View {
    @ObservedObject var workspace: WorkspaceModel
    @Environment(\.dismiss) private var dismiss
    @AppStorage("extraKeys") private var keys = "Esc,Tab,Ctrl-C,Ctrl-D,↑,↓,←,→"
    @AppStorage("useVolumeBudget") private var volume = false
    @AppStorage("vulkanCompute") private var vulkan = false
    @AppStorage("bridgeEnabled") private var bridge = true
    @AppStorage("guestMemoryMiB") private var memoryTotal = 1024
    @AppStorage("guestProcessMemoryMiB") private var memoryProcess = 512
    private var memoryChoices: [Int] {
        [256, 512, 1024, 2048, 3072, 4096, 6144, 8192, 12288, 16384].filter {
            (UInt64($0) << 20) <= ProcessInfo.processInfo.physicalMemory
        }
    }
    @State private var resetConfirmation = false
    var body: some View {
        NavigationStack {
            Form {
                Section("键盘") { TextField("附加按键，以逗号分隔", text: $keys); Text("支持 Esc、Tab、Ctrl-C、Ctrl-D 和方向键，也可添加常用文本。").font(.caption) }
                Section("本地内存") {
                    Picker("Linux 总预算", selection: $memoryTotal) {
                        ForEach(memoryChoices, id: \.self) { value in
                            Text(ByteCountFormatter.string(fromByteCount: Int64(value) << 20, countStyle: .binary)).tag(value)
                        }
                    }
                    .onChange(of: memoryTotal) { _, value in memoryProcess = min(memoryProcess, value) }
                    Picker("单进程预算", selection: $memoryProcess) {
                        ForEach(memoryChoices.filter { $0 <= memoryTotal }, id: \.self) { value in
                            Text(ByteCountFormatter.string(fromByteCount: Int64(value) << 20, countStyle: .binary)).tag(value)
                        }
                    }
                    Text("默认总预算 1 GiB、单进程 512 MiB。内存按需使用，不会预先分配；请为 iPadOS 和其他应用留出空间。下次打开本地终端生效。").font(.caption)
                }
                Section("本地磁盘") {
                    Toggle("使用设备卷容量作为存储预算", isOn: $volume)
                    Text("默认预算 16 GiB；启用后按应用所在卷的总容量计算。实际可用空间仍受系统剩余容量限制，下次打开本地终端生效。").font(.caption)
                    Button("重置 Linux 环境",role:.destructive) {resetConfirmation=true}.disabled(workspace.resetting)
                }
                Section("Linux 与 iPad") {
                    Toggle("允许 Linux 发起原生访问请求", isOn: $bridge)
                    Text("剪贴板、打开链接、设备信息及 Files 复制均需逐次允许。关闭后新请求会被拒绝。").font(.caption)
                    if let directories = workspace.directories {
                        NavigationLink("Files 目录授权") { FolderGrantsView(store: directories) }
                    }
                }
                Section("后台行为") { Text("进入后台后暂停本地 Linux 并断开 SSH；返回前台后继续本地会话。Files 采用独立副本导入和导出。").font(.caption) }
                Section("GPU") {
                    Toggle("Vulkan 计算与图形（实验）", isOn: $vulkan)
                    Text("下次打开本地终端生效。需在 Linux 安装 vulkan-loader；支持选定计算和离屏图形接口；Vulkan 程序可通过标准 display/swapchain 接口提交到 App 内的显示窗口；拖动和缩放窗口不改变画布分辨率。").font(.caption)
                }
            }.navigationTitle("设置").toolbar { Button("完成") { dismiss() } }
                .alert("重置 Linux 环境？",isPresented:$resetConfirmation) {
                    Button("删除并重新初始化",role:.destructive) {dismiss(); workspace.resetLocal()}
                    Button("取消",role:.cancel) {}
                } message: {Text("将删除 Linux 中所有软件、文件和配置，并重新下载 Alpine。SSH 设置和 Files 目录授权会保留。")}
        }
    }
}
