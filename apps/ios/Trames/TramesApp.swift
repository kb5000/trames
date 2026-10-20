// SPDX-License-Identifier: GPL-3.0-or-later
import SwiftUI
import TramesKit
@main
struct TramesApp: App {
    @StateObject private var workspace = WorkspaceModel()
    @Environment(\.scenePhase) private var scenePhase
    var body: some Scene {
        WindowGroup {
            GeometryReader { proxy in
                WorkspaceKeyboardContainer(workspace: workspace)
                    .frame(width:proxy.size.width,height:proxy.size.height)
            }
                .ignoresSafeArea(.keyboard)
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
    @State private var filesSheet = false
    @State private var settingsSheet = false
    @State private var sessionActions = false
    @State private var frames: [String:CGRect] = [:]
    @State private var draggedTab: WorkspaceItemID?
    @State private var dragLocation = CGPoint.zero
    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Text("TRAMES").font(.system(.headline, design: .monospaced)).foregroundStyle(.mint)
                ScrollView(.horizontal,showsIndicators:false) {
                    HStack(spacing:4) {
                        ForEach(workspace.layout.tabs,id:\.self) { item in
                            if let session=workspace.session(for:item) {
                                HStack(spacing:0) {
                                    Button { workspace.select(item) } label: {
                                        WorkspaceTabLabel(session:session,item:item).lineLimit(1)
                                            .padding(.horizontal,8).frame(minHeight:44).contentShape(Rectangle())
                                    }.buttonStyle(.plain)
                                        .accessibilityIdentifier("tab-\(item.token)")
                                        .simultaneousGesture(DragGesture(minimumDistance:10,coordinateSpace:.named("workspace"))
                                            .onChanged { value in draggedTab=item;dragLocation=value.location }
                                            .onEnded { value in finishTabDrag(item,at:value.location) })
                                        .contextMenu {
                                            Button("移到浮动窗口",systemImage:"rectangle.on.rectangle") { workspace.detach(item) }
                                            Button("关闭",role:.destructive) { workspace.closeItem(item) }
                                        }
                                    Button { workspace.closeItem(item) } label: {
                                        Image(systemName:"xmark").font(.system(size:12))
                                            .frame(width:44,height:44).contentShape(Rectangle())
                                    }.buttonStyle(.plain).accessibilityLabel("关闭标签页")
                                        .accessibilityIdentifier("close-tab-\(item.token)")
                                }
                                .background(workspace.layout.selected == item ? Color.white.opacity(0.12) : .clear)
                                .clipShape(RoundedRectangle(cornerRadius:6))
                                .workspaceFrame(item.token)
                            }
                        }
                    }
                }.workspaceFrame("tabs")
                Button { sessionActions = true } label: {
                    Label("新建或管理会话", systemImage: "plus")
                        .labelStyle(.iconOnly).frame(minWidth: 44, minHeight: 44)
                }.confirmationDialog("新建或管理会话", isPresented: $sessionActions, titleVisibility: .visible) {
                    Button("本地 Alpine 终端", systemImage: "terminal") { workspace.openLocal() }
                    Button("Files", systemImage: "folder") { filesSheet = true }
                    Button("设置", systemImage: "gear") { settingsSheet = true }
                }

            }.padding(.horizontal,12).padding(.vertical,6).workspaceFrame("header")
            GeometryReader { proxy in
                ZStack(alignment:.topLeading) {
                    // A terminal/Metal view has exactly one SwiftUI host for its
                    // entire lifetime. Docking changes geometry, never parents.
                    ForEach(workspace.items,id:\.self) { item in
                        WorkspaceWindow(workspace:workspace,item:item,viewport:proxy.size)
                            .zIndex(workspace.layout.placement(item) == .floating
                                ? Double((workspace.layout.floating.firstIndex(of:item) ?? 0)+2)
                                : workspace.layout.selected == item ? 1 : 0)
                    }
                }.frame(width:proxy.size.width,height:proxy.size.height,alignment:.topLeading)
                .overlay {
                    if workspace.layout.selected == nil && workspace.layout.floating.isEmpty {
                        ContentUnavailableView("打开一个终端",systemImage:"terminal",description:Text("从顶部标签栏恢复窗口，或打开本地 Alpine。"))
                    }
                }
            .onAppear { workspace.updateViewport(proxy.size) }
            .onChange(of:proxy.size) { _,size in workspace.updateViewport(size) }
            }.workspaceFrame("body")
        }
        .coordinateSpace(name:"workspace")
        .onPreferenceChange(WorkspaceFramesKey.self) { frames=$0 }
        .overlay(alignment:.topLeading) {
            if let item=draggedTab,let session=workspace.session(for:item) {
                WorkspaceTabLabel(session:session,item:item).padding(10).background(.ultraThinMaterial)
                    .clipShape(RoundedRectangle(cornerRadius:6)).offset(x:dragLocation.x-40,y:dragLocation.y-18).allowsHitTesting(false)
            }
        }
        .background(.black)
        .background { if let owner=workspace.localOwner {BridgeConsentPresenter(controller:owner.bridge)} }
        .sheet(isPresented: $filesSheet) { if let session = workspace.current { TransferView(session: session) } }
        .sheet(isPresented: $settingsSheet) { SettingsView(workspace: workspace) }
        .alert("Trames", isPresented: Binding(get: { workspace.error != nil }, set: { if !$0 { workspace.error = nil } })) {
            Button("好") { workspace.error = nil }
        } message: { Text(workspace.error ?? "") }
    }
    private func finishTabDrag(_ item: WorkspaceItemID, at point: CGPoint) {
        defer { draggedTab=nil }
        let body=frames["body"] ?? .zero
        if body.contains(point) {
            workspace.detach(item,at:CGPoint(x:max(0,point.x-body.minX-60),y:max(0,point.y-body.minY-20)))
        } else if (frames["header"] ?? .zero).contains(point) {
            let target=workspace.layout.tabs.first { candidate in
                candidate != item && (frames[candidate.token].map { point.x < $0.midX } ?? false)
            }
            workspace.dock(item,before:target)
        }
    }

}
struct SessionView: View {
    @ObservedObject var workspace: WorkspaceModel
    @ObservedObject var session: TerminalSession
    var body: some View {
        VStack(spacing: 0) {
            GeometryReader { proxy in
                TerminalPane(session:session,workspace:workspace,onFocus:{ [weak workspace, weak session] in if let session { workspace?.focus(.terminal(session.id)) } })
                    .frame(width:proxy.size.width,height:proxy.size.height)
            }.frame(minHeight:0,maxHeight:.infinity).clipped()
            HStack {
                Text(session.state).lineLimit(2).accessibilityIdentifier("session-state")
                Spacer()
                if let cpu = session.cpuPercent {
                    Text("CPU \(cpu)%").monospacedDigit().accessibilityIdentifier("cpu-usage")
                        .accessibilityHint("App CPU 用量，一个核心满载为百分之百")
                }
                if let capacity = session.capacity {
                    Text("\(ByteCountFormatter.string(fromByteCount: Int64(capacity.used_bytes), countStyle: .binary)) / \(ByteCountFormatter.string(fromByteCount: Int64(capacity.budget_bytes), countStyle: .binary))")
                }
            }.font(.caption).foregroundStyle(.secondary).padding(.horizontal,12)
                .padding(.vertical,6).fixedSize(horizontal:false,vertical:true)
                .layoutPriority(1)
        }
    }
}
struct SettingsView: View {
    @ObservedObject var workspace: WorkspaceModel
    @Environment(\.dismiss) private var dismiss
    @AppStorage("useVolumeBudget") private var volume = false
    @AppStorage("vulkanCompute") private var vulkan = true
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
                    Text("文本剪贴板自动与 iPad 互通，读取由 iPadOS 请求权限。其他原生请求仍需逐次允许；关闭后所有新请求都会被拒绝。").font(.caption)
                    if let directories = workspace.directories {
                        NavigationLink("Files 目录授权") { FolderGrantsView(store: directories) }
                    }
                }
                Section("后台行为") { Text("进入后台后暂停本地 Linux；返回前台后继续。Files 采用独立副本导入和导出。").font(.caption) }
                Section("GPU") {
                    Toggle("Vulkan 计算与图形（实验）", isOn: $vulkan)
                    Text("下次打开本地终端生效。需在 Linux 安装 vulkan-loader；支持选定计算、图形和 Wayland 接口。Wayland 应用各自显示为 App 内窗口，调整窗口大小会请求应用改变分辨率。窗口也可停靠到顶部标签栏。").font(.caption)
                }
            }.navigationTitle("设置").toolbar { Button("完成") { dismiss() } }
                .alert("重置 Linux 环境？",isPresented:$resetConfirmation) {
                    Button("删除并重新初始化",role:.destructive) {dismiss(); workspace.resetLocal()}
                    Button("取消",role:.cancel) {}
                } message: {Text("将删除 Linux 中所有软件、文件和配置，并重新下载 Alpine。Files 目录授权会保留。")}
        }
    }
}
