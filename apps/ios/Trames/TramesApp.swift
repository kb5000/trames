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
                Menu {
                    Button("本地 Alpine", systemImage: "terminal") { workspace.openLocal() }
                    Button("SSH 连接", systemImage: "network") { sshSheet = true }
                    Button("Files / SFTP", systemImage: "folder") { filesSheet = true }
                    Button("设置", systemImage: "gear") { settingsSheet = true }
                } label: {
                    Label("新建或管理会话", systemImage: "plus")
                        .labelStyle(.iconOnly).frame(minWidth: 44, minHeight: 44)
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
        .sheet(isPresented: $settingsSheet) { SettingsView() }
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
                }.padding(6)
            }
            HStack {
                Text(session.state).lineLimit(2).accessibilityIdentifier("session-state")
                Spacer()
                if let capacity = session.capacity {
                    Text("\(ByteCountFormatter.string(fromByteCount: Int64(capacity.used_bytes), countStyle: .binary)) / \(ByteCountFormatter.string(fromByteCount: Int64(capacity.budget_bytes), countStyle: .binary))")
                }
            }.font(.caption).foregroundStyle(.secondary).padding(.horizontal, 12).padding(.vertical, 6)
        }
    }
}
struct SettingsView: View {
    @Environment(\.dismiss) private var dismiss
    @AppStorage("extraKeys") private var keys = "Esc,Tab,Ctrl-C,Ctrl-D,↑,↓,←,→"
    @AppStorage("useVolumeBudget") private var volume = false
    var body: some View {
        NavigationStack {
            Form {
                Section("键盘") { TextField("附加按键，以逗号分隔", text: $keys); Text("支持 Esc、Tab、Ctrl-C、Ctrl-D 和方向键，也可添加常用文本。").font(.caption) }
                Section("本地磁盘") {
                    Toggle("使用设备卷容量作为存储预算", isOn: $volume)
                    Text("默认预算 16 GiB；启用后按应用所在卷的总容量计算。实际可用空间仍受系统剩余容量限制，下次打开本地终端生效。").font(.caption)
                }
                Section("后台行为") { Text("进入后台后暂停本地 Linux 并断开 SSH；返回前台后继续本地会话。Files 采用独立副本导入和导出。").font(.caption) }
            }.navigationTitle("设置").toolbar { Button("完成") { dismiss() } }
        }
    }
}
