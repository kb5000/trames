// SPDX-License-Identifier: GPL-3.0-or-later
import SwiftUI
import TramesKit
@MainActor
final class WorkspaceModel: ObservableObject {
    @Published var sessions: [TerminalSession] = []
    @Published var selected: UUID?
    @Published var challenge: HostKeyChallenge?
    @Published var error: String?
    @Published var resetting = false
    private(set) var environment: EnvironmentStore?
    private(set) var knownHosts: KnownHosts?
    private(set) var directories: ScopedDirectories?
    init() {
        do {
            let environment = try EnvironmentStore(); self.environment = environment
            directories = try ScopedDirectories(file: environment.root.appendingPathComponent("folder-grants.json"))
            Task {
                do { knownHosts = try await environment.knownHosts(); openLocal() }
                catch { self.error = error.localizedDescription }
            }
        } catch { self.error = error.localizedDescription }
    }
    var current: TerminalSession? { sessions.first { $0.id == selected } }
    var local: TerminalSession? { sessions.first { $0.local != nil || $0.title == "Alpine" } }
    func openLocal() {
        guard !resetting else { return }
        if let local { selected = local.id; return }
        guard let environment else { return }
        let session = TerminalSession(title: "Alpine")
        session.bridge.directories = directories
        sessions.append(session); selected = session.id
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
        sessions.append(session); selected = session.id
        session.startSSH(host: host, credential: credential, knownHosts: knownHosts)
    }
    func close(_ session: TerminalSession) {
        session.close(); sessions.removeAll { $0.id == session.id }
        if selected == session.id { selected = sessions.last?.id }
    }
    func trust() {
        guard let challenge, let knownHosts else { return }
        do { try knownHosts.trust(challenge); self.challenge = nil }
        catch { self.error = error.localizedDescription }
    }
    func background() {
        for session in sessions {
            session.pause(true)
            if session.ssh != nil { session.close() }
        }
    }
    func foreground() { for session in sessions { session.pause(false) } }
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
