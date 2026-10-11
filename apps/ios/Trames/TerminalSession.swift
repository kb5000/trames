// SPDX-License-Identifier: GPL-3.0-or-later
import SwiftUI
import SwiftTerm
import TramesKit
@MainActor
final class TerminalSession: NSObject, ObservableObject, Identifiable, TerminalViewDelegate {
    let id = UUID()
    let terminal = TerminalViewport(frame: .zero, font: .monospacedSystemFont(ofSize: 14, weight: .regular))
    @Published var title: String
    @Published var state = "连接中…"
    @Published var capacity: LocalStatus?
    var local: LocalSession?
    var ssh: SSHSession?
    var onChallenge: ((HostKeyChallenge) -> Void)?
    private var task: Task<Void,Never>?
    private var sender: Task<Void,Never>?
    private var pendingInput = Data()
    private var timer: Timer?
    private var lastPhase: String?
    private var closed = false
    private var paused = false
    init(title: String) {
        self.title = title; super.init()
        terminal.terminalDelegate = self
        terminal.backgroundColor = .black
        terminal.getTerminal().changeScrollback(1000)
    }
    func startLocal(_ environment: EnvironmentStore, useVolumeBudget: Bool) {
        task = Task { [weak self] in
            do {
                let local = try await environment.start(useVolumeBudget: useVolumeBudget)
                guard let self, !self.closed else { local.close(); return }
                self.local = local
                try local.pause(paused)
                try local.resize(columns: max(1,terminal.getTerminal().cols), rows: max(1,terminal.getTerminal().rows))
                timer = Timer.scheduledTimer(withTimeInterval: 0.02, repeats: true) { [weak self] _ in
                    Task { @MainActor in self?.pollLocal() }
                }
            } catch { self?.fail(error) }
        }
    }
    private func pollLocal() {
        guard let local, !closed else { return }
        do {
            let output = try local.receive()
            if !output.isEmpty { terminal.feed(byteArray: Array(output)[...]) }
            if !pendingInput.isEmpty {
                let count = try local.send(pendingInput)
                pendingInput.removeFirst(count)
            }
            let status = try local.status()
            if status != capacity { capacity = status }
            if status.phase != lastPhase {
                lastPhase = status.phase
                state = status.phase == "running" ? "Alpine · 本地" : status.phase
                if let error = status.error { fail(TramesError.message(error)) }
                if status.phase == "exited" { state = "已退出 · \(status.exit_code ?? 0)"; timer?.invalidate() }
            }
        } catch { fail(error); timer?.invalidate() }
    }
    func startSSH(host: SSHHost, credential: SSHCredential, knownHosts: KnownHosts) {
        let connection = SSHSession(); ssh = connection
        task = Task { [weak self] in
            do {
                try await connection.run(host: host, credential: credential, knownHosts: knownHosts,
                    onChallenge: { [weak self] challenge in Task { @MainActor in self?.onChallenge?(challenge) } },
                    output: { [weak self] bytes in
                        await MainActor.run {
                            guard let self, !self.closed else { return }
                            self.state = "SSH · \(host.hostname)"
                            self.terminal.feed(byteArray: Array(bytes)[...])
                        }
                    })
                self?.state = "SSH 已断开"
            } catch { self?.fail(error) }
        }
    }
    func send(_ data: Data) {
        guard !closed, pendingInput.count + data.count <= 65536 else { state = "输入队列已满，请稍后重试"; return }
        pendingInput.append(data)
        if let ssh, sender == nil {
            sender = Task { [weak self] in
                guard let self else { return }
                defer { sender = nil }
                while !pendingInput.isEmpty && !closed {
                    let data = pendingInput; pendingInput.removeAll(keepingCapacity: true)
                    do { try await ssh.send(data) } catch { fail(error); break }
                }
            }
        }
    }
    func pause(_ paused: Bool) { self.paused = paused; try? local?.pause(paused) }
    func close() {
        guard !closed else { return }; closed = true
        timer?.invalidate(); timer = nil; task?.cancel(); sender?.cancel()
        local?.close(); local = nil
        if let ssh { Task { await ssh.close() } }; ssh = nil; pendingInput.removeAll()
        state = "已关闭"
    }
    func fail(_ error: Error) {
        guard !closed else { return }
        state = error.localizedDescription
    }
    func sizeChanged(source: TerminalView, newCols: Int, newRows: Int) {
        try? local?.resize(columns: newCols, rows: newRows)
        if let ssh { Task { try? await ssh.resize(columns: newCols, rows: newRows) } }
    }
    func setTerminalTitle(source: TerminalView, title: String) { self.title = String(title.prefix(80)) }
    func hostCurrentDirectoryUpdate(source: TerminalView, directory: String?) {}
    func send(source: TerminalView, data: ArraySlice<UInt8>) { send(Data(data)) }
    func scrolled(source: TerminalView, position: Double) {}
    // Remote OSC clipboard and URL requests require a later consent broker.
    func requestOpenLink(source: TerminalView, link: String, params: [String:String]) {}
    func bell(source: TerminalView) {}
    func clipboardCopy(source: TerminalView, content: Data) {}
    func clipboardRead(source: TerminalView) -> Data? { nil }
    func iTermContent(source: TerminalView, content: ArraySlice<UInt8>) {}
    func rangeChanged(source: TerminalView, startY: Int, endY: Int) {}
}
struct TerminalPane: UIViewRepresentable {
    @ObservedObject var session: TerminalSession
    func makeUIView(context: Context) -> TerminalView { session.terminal }
    func updateUIView(_ view: TerminalView, context: Context) {}
}
