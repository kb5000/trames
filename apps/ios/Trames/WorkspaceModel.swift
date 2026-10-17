// SPDX-License-Identifier: GPL-3.0-or-later
import SwiftUI
import TramesKit
@MainActor
final class WorkspaceModel: ObservableObject {
    @Published var sessions: [TerminalSession] = []
    @Published private(set) var layout = WorkspaceTabs()
    private var observedModes: [WorkspaceItemID:String] = [:]
    @Published var challenge: HostKeyChallenge?
    @Published var error: String?
    @Published var resetting = false
    private var benchmarkRunning = false
    private var appActive = true
    private(set) var environment: EnvironmentStore?
    private(set) var knownHosts: KnownHosts?
    private(set) var directories: ScopedDirectories?
    init(prepareEnvironment: Bool = true) {
        #if DEBUG
        if ProcessInfo.processInfo.arguments.contains("--workspace-ui-acceptance") {
            let session=TerminalSession(title:"Alpine",isLocal:true)
            session.state="工作区交互验收";add(session)
            session.terminal.feed(text:"\u{1b}]0;\u{7}")
            let data=Data("[{\"id\":7,\"connection\":1,\"title\":\"测试图形窗口\",\"app_id\":\"workspace-test\",\"width\":400,\"height\":280,\"revision\":1,\"mode\":\"floating\",\"bounds\":[0,0,400,280]}]".utf8)
            session.waylandWindows=(try? JSONDecoder().decode([LocalWaylandWindow].self,from:data)) ?? []
            session.onWaylandChanged?(session.waylandWindows)
            return
        }
        #endif
        guard prepareEnvironment else { return }
        do {
            let environment = try EnvironmentStore(); self.environment = environment
            directories = try ScopedDirectories(file: environment.root.appendingPathComponent("folder-grants.json"))
            Task {
                do { knownHosts = try await environment.knownHosts(); openLocal() }
                catch { self.error = error.localizedDescription }
            }
        } catch { self.error = error.localizedDescription }
    }
    var current: TerminalSession? { layout.selected.flatMap { session(for: $0) } ?? local }
    var local: TerminalSession? { sessions.first { $0.isLocal } }
    func session(for id: WorkspaceItemID) -> TerminalSession? { sessions.first { $0.id == id.session } }
    func window(for id: WorkspaceItemID) -> LocalWaylandWindow? {
        guard case .wayland(_, let window) = id else { return nil }
        return session(for:id)?.waylandWindows.first { $0.id == window }
    }
    func add(_ session: TerminalSession) {
        session.onWaylandChanged = { [weak self, weak session] windows in
            guard let self, let session else { return }; self.synchronize(session, windows:windows)
        }
        sessions.append(session)
        change { $0.insert(.terminal(session.id), as:.tabbed) }
    }
    private func change(protocolFocus: Bool = true, _ update: (inout WorkspaceTabs) -> Void) {
        var next=layout; update(&next)
        guard next != layout else { return }
        let old=layout.focused; layout=next
        if old != next.focused {
            for session in sessions where session.isLocal {
                if let focused=next.focused, case .wayland(let owner, let window) = focused, owner == session.id {
                    if protocolFocus { session.waylandCommand(["op":"focus","window":window]) }
                } else { session.waylandCommand(["op":"focus","window":NSNull()]) }
            }
        }
    }
    func focus(_ id: WorkspaceItemID, protocolFocus: Bool = true) { change(protocolFocus:protocolFocus) { $0.focus(id) } }
    func activate(_ id: WorkspaceItemID) {
        if layout.placement(id) == .minimized { restore(id) }
        else if layout.placement(id) == .tabbed { select(id) }
        else { focus(id) }
    }
    func select(_ id: WorkspaceItemID) { change { $0.select(id) } }
    func dock(_ id: WorkspaceItemID, before target: WorkspaceItemID? = nil) {
        change { $0.dock(id,before:target) }; nativeMode(id,"maximized")
    }
    func detach(_ id: WorkspaceItemID, at point: CGPoint? = nil) {
        change { $0.detach(id,at:point) }; nativeMode(id,"floating")
    }
    func minimize(_ id: WorkspaceItemID) { change { $0.minimize(id) }; nativeMode(id,"minimized") }
    func restore(_ id: WorkspaceItemID) {
        change { $0.restore(id) }
        nativeMode(id,layout.placement(id) == .tabbed ? "maximized" : "floating")
    }
    func saveFrame(_ id: WorkspaceItemID, _ frame: DisplayWindowGeometry) { change { $0.frames[id]=frame } }
    func closeItem(_ id: WorkspaceItemID) {
        if case .terminal = id, let session=session(for:id) { close(session) }
        else if case .wayland(_,let window)=id { session(for:id)?.waylandCommand(["op":"close","window":window]) }
    }
    private func nativeMode(_ id: WorkspaceItemID, _ mode: String) {
        if case .wayland(_,let window)=id { session(for:id)?.waylandMode(window,mode) }
    }
    private func synchronize(_ session: TerminalSession, windows: [LocalWaylandWindow]) {
        let live=Set(windows.map { WorkspaceItemID.wayland(session.id,$0.id) })
        change { next in
            for id in Array(observedModes.keys) where id.session == session.id && !live.contains(id) {
                next.close(id); observedModes.removeValue(forKey:id)
            }
            for window in windows {
                let id=WorkspaceItemID.wayland(session.id,window.id)
                let oldMode=observedModes.updateValue(window.mode,forKey:id)
                let placement:WorkspaceTabs.Placement = window.mode == "maximized" ? .tabbed : window.mode == "minimized" ? .minimized : .floating
                if next.placement(id) == nil { next.insert(id,as:placement) }
                else if oldMode != window.mode && next.placement(id) != placement {
                    switch placement {
                    case .tabbed: next.dock(id)
                    case .floating: next.detach(id)
                    case .minimized: next.minimize(id)
                    }
                }
            }
        }
    }
    func openLocal() {
        guard !resetting else { return }
        if let local { activate(.terminal(local.id)); return }
        guard let environment else { return }
        let session = TerminalSession(title: "Alpine", isLocal:true)
        session.pause(benchmarkRunning || !appActive)
        session.bridge.directories = directories
        add(session)
        session.startLocal(environment, useVolumeBudget: UserDefaults.standard.bool(forKey: "useVolumeBudget"))
    }
    func resetLocal() {
        guard let environment, !resetting else {return}
        resetting=true
        if let local {close(local)}
        Task {
            do { try await environment.reset(); resetting=false; openLocal() }
            catch {resetting=false; self.error=error.localizedDescription}
        }
    }
    func connect(host: SSHHost, credential: SSHCredential) {
        guard sessions.count < 8, let knownHosts else { error = "最多同时打开 8 个终端"; return }
        let session = TerminalSession(title: host.alias)
        session.onChallenge = { [weak self] challenge in self?.challenge = challenge }
        add(session)
        session.startSSH(host: host, credential: credential, knownHosts: knownHosts)
    }
    func close(_ session: TerminalSession) {
        session.close(); sessions.removeAll { $0.id == session.id }
        change { $0.closeSession(session.id) }
        observedModes = observedModes.filter { $0.key.session != session.id }
    }
    func trust() {
        guard let challenge, let knownHosts else { return }
        do { try knownHosts.trust(challenge); self.challenge = nil }
        catch { self.error = error.localizedDescription }
    }
    func background() {
        appActive = false
        for session in sessions {
            session.pause(true)
            if session.ssh != nil { session.close() }
        }
    }
    func foreground() {
        appActive = true
        for session in sessions { session.pause(benchmarkRunning && session.ssh == nil) }
    }
    func setBenchmarkRunning(_ running: Bool) {
        benchmarkRunning = running
        local?.pause(running || !appActive)
    }
    func guestText(_ path: String) async throws -> String {
        guard let local = local?.local else { throw TramesError.message("请先启动本地 Alpine 环境") }
        let stage = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: stage) }
        try await local.transferAsync(guest: path, host: stage, export: true, maximumBytes: 65536)
        let size = try stage.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? Int.max
        guard size <= 65536 else { throw TramesError.message("SSH 配置或密钥超过 64 KiB") }
        return try String(contentsOf: stage, encoding: .utf8)
    }
}
