// SPDX-License-Identifier: GPL-3.0-or-later
import SwiftUI
import SwiftTerm
import TramesKit
import Darwin
@MainActor
final class TerminalSession: NSObject, ObservableObject, Identifiable, TerminalViewDelegate {
    let id = UUID()
    let terminal = TerminalViewport(frame: .zero, font: .monospacedSystemFont(ofSize: 14, weight: .regular))
    let fallbackTitle: String
    let isLocal: Bool
    var onWaylandChanged: (([LocalWaylandWindow]) -> Void)?
    @Published var title: String
    @Published var state = "连接中…"
    @Published var capacity: LocalStatus?
    @Published var waylandWindows: [LocalWaylandWindow] = []
    @Published var waylandText: LocalWaylandText?
    private var waylandRevision: UInt64?
    #if DEBUG
    var onWaylandCommand: (([String:Any]) -> Void)?
    var onTerminalOutput: ((Data) -> Void)?
    #endif
    private var storedLocal: LocalSession?
    private let storedBridge = BridgeController()
    private(set) var environmentOwner: TerminalSession?
    private(set) var terminalID: UInt64? = 1
    var local: LocalSession? {
        get { environmentOwner?.local ?? storedLocal }
        set { storedLocal=newValue }
    }
    var bridge: BridgeController { environmentOwner?.bridge ?? storedBridge }
    @Published private(set) var cpuPercent: Int?
    private var cpuSample: (wall:TimeInterval,cpu:TimeInterval)?
    private var task: Task<Void,Never>?
    private var pendingInput = Data()
    private var timer: Timer?
    private var lastPhase: String?
    private var closed = false
    private var terminalClosed = false
    private var terminalExited = false
    private var startupError: Error?
    private var paused = false
    private var timezoneObserver: NSObjectProtocol?
    private var clipboardObserver: NSObjectProtocol?
    private var terminalSize:(columns:Int,rows:Int)?
    init(title: String, isLocal: Bool = false) {
        self.fallbackTitle = title.isEmpty ? "终端" : title
        self.isLocal = isLocal
        self.title = self.fallbackTitle; super.init()
        terminal.terminalDelegate = self
        terminal.onGridLayout = { [weak self] columns,rows in self?.synchronizeTerminalSize(columns:columns,rows:rows) }
        terminal.accessibilityIdentifier = "terminal-viewport"
        terminal.backgroundColor = .black
        timezoneObserver = NotificationCenter.default.addObserver(forName: .NSSystemTimeZoneDidChange, object: nil, queue: .main) { [weak self] _ in
            Task { @MainActor in self?.synchronizeTimezone() }
        }
        clipboardObserver=NotificationCenter.default.addObserver(forName:UIPasteboard.changedNotification,object:nil,queue:.main) { [weak self] _ in
            Task { @MainActor in self?.synchronizeClipboard() }
        }
        terminal.getTerminal().changeScrollback(1000)
        #if DEBUG
        if ProcessInfo.processInfo.arguments.contains("--ui-acceptance") {
            terminal.getTerminal().setCursorStyle(.steadyBlock)
        }
        #endif
    }
    func startLocal(_ environment: EnvironmentStore, useVolumeBudget: Bool) {
        state="正在准备 Alpine…"
        task = Task { [weak self] in
            do {
                let gpu = UserDefaults.standard.bool(forKey: "vulkanCompute")
                let memory = UserDefaults.standard.object(forKey: "guestMemoryMiB") as? Int ?? 1024
                let processMemory = UserDefaults.standard.object(forKey: "guestProcessMemoryMiB") as? Int ?? 512
                let local = try await environment.start(useVolumeBudget: useVolumeBudget, vulkan: gpu, memoryBudgetMiB: memory, processMemoryBudgetMiB: min(processMemory, memory))
                guard let self, !self.closed else { local.close(); return }
                self.local = local
                bridge.attach(local)
                try local.pause(paused)
                terminalSize=nil
                synchronizeTerminalSize(columns:max(1,terminal.getTerminal().cols),rows:max(1,terminal.getTerminal().rows))
                if terminalClosed { try? local.closeTerminal(1);terminalID=nil }
                startPolling()
            } catch { self?.startupError=error;self?.fail(error) }
        }
    }
    func attachTerminal(to owner: TerminalSession) {
        environmentOwner=owner;terminalID=nil
        task=Task { [weak self] in
            do {
                while owner.local == nil {
                    try Task.checkCancellation()
                    if let error=owner.startupError {throw error}
                    if owner.closed {throw TramesError.message("Linux 环境已关闭")}
                    try await Task.sleep(for:.milliseconds(20))
                }
                try Task.checkCancellation()
                guard let self,!closed,!terminalClosed,let local=owner.local else {return}
                terminalID=try local.openTerminal()
                terminalSize=nil
                synchronizeTerminalSize(columns:terminal.getTerminal().cols,rows:terminal.getTerminal().rows)
                startPolling()
            } catch {self?.fail(error)}
        }
    }
    func startPolling() {
        timer=Timer.scheduledTimer(withTimeInterval:0.02,repeats:true) { [weak self] _ in
            Task { @MainActor in self?.pollLocal() }
        }
    }
    private func pollLocal() {
        guard let local, !closed else { return }
        do {
            if !terminalClosed,let terminalID {
              do {
                let status=try local.terminalStatus(terminalID)
                if status.phase != lastPhase {
                    lastPhase=status.phase
                    state=status.phase == "running" ? "Alpine · 本地" : status.phase
                    terminalExited=["exited","failed","cancelled"].contains(status.phase)
                    if terminalExited {pendingInput.removeAll();state=status.error ?? "已退出"}
                }
                let output = try local.receive(terminal:terminalID)
                #if DEBUG
                onTerminalOutput?(output)
                #endif
                if !output.isEmpty { terminal.feed(byteArray: Array(output)[...]) }
                if !pendingInput.isEmpty && !terminalExited {
                    let count = try local.send(pendingInput,terminal:terminalID)
                    pendingInput.removeFirst(count)
                }
              } catch {
                terminalExited=true;pendingInput.removeAll();fail(error)
              }
            }
            if environmentOwner == nil {
                try bridge.poll()
                applyWaylandStatus(try local.waylandStatus())
                sampleCPU()
                FileProviderService.poll()
            } else if cpuPercent != environmentOwner?.cpuPercent {cpuPercent=environmentOwner?.cpuPercent}
            let status = try local.status()
            if status != capacity { capacity = status }
            if let error=status.error {fail(TramesError.message(error))}
            if ["failed","cancelled","exited"].contains(status.phase) {timer?.invalidate()}
        } catch { fail(error); timer?.invalidate() }
    }
    /// One host getrusage sample every two seconds, on the existing timer.
    /// This includes the interpreter/native workers; 100% means one busy core.
    private func sampleCPU() {
        guard !paused else { return }
        let wall=ProcessInfo.processInfo.systemUptime
        if let previous=cpuSample,wall-previous.wall<2 { return }
        var usage=rusage()
        guard getrusage(RUSAGE_SELF,&usage) == 0 else { return }
        let cpu=Double(usage.ru_utime.tv_sec+usage.ru_stime.tv_sec)
            + Double(usage.ru_utime.tv_usec+usage.ru_stime.tv_usec)/1_000_000
        if let previous=cpuSample,wall>previous.wall {
            let percent=Int(max(0,(cpu-previous.cpu)/(wall-previous.wall)*100).rounded())
            if cpuPercent != percent { cpuPercent=percent }
        }
        cpuSample=(wall,cpu)
    }
    func applyWaylandStatus(_ status: LocalWaylandStatus) {
        guard status.revision != waylandRevision else { return }
        waylandRevision=status.revision
        if waylandWindows.count != status.windows.count
            || !zip(waylandWindows,status.windows).allSatisfy({ pair in pair.0.hasSameLayout(as:pair.1) }) {
            waylandWindows=status.windows
        }
        if waylandText != status.text { waylandText=status.text }
        // Mode acknowledgements can have the same layout as the previous
        // snapshot (e.g. minimize/restore between polls). Reconcile them without
        // publishing pixel-only revisions to the view tree.
        onWaylandChanged?(status.windows)
    }
    func waylandCommand(_ command: [String:Any]) {
        #if DEBUG
        onWaylandCommand?(command)
        #endif
        do { try local?.waylandCommand(command) } catch { fail(error) }
    }
    func waylandMode(_ window: UInt64, _ mode: String) {
        waylandCommand(["op":"mode","window":window,"mode":mode])
    }
    func send(_ data: Data) {
        guard !closed,!terminalClosed,!terminalExited else {return}
        guard pendingInput.count + data.count <= 65536 else { state = "输入队列已满，请稍后重试"; return }
        pendingInput.append(data)

    }
    func pause(_ paused: Bool) {
        self.paused = paused
        cpuSample=nil
        if paused { cpuPercent=nil }
        if environmentOwner == nil {
            if paused { waylandCommand(["op":"focus","window":NSNull()]) }
            try? local?.pause(paused)
            if !paused { synchronizeTimezone();synchronizeClipboard() }
        }
    }
    private func synchronizeClipboard() {
        guard environmentOwner == nil,!paused,local != nil,UserDefaults.standard.object(forKey:"bridgeEnabled") as? Bool != false else { return }
        // change notifications carry no clipboard text; actual reads happen
        // only when a Wayland client requests the text offer.
        try? local?.waylandCommand(["op":"clipboard_changed"])
    }
    private func synchronizeTimezone() {
        if environmentOwner == nil,!paused, let local {
            Task { [weak self] in
                do {
                    try await Task.detached(priority: .utility) { try local.synchronizeTimezone() }.value
                } catch { self?.fail(error) }
            }
        }
    }
    func close() {
        guard !closed else { return }; closed = true
        if let timezoneObserver { NotificationCenter.default.removeObserver(timezoneObserver) }
        timezoneObserver = nil
        if let clipboardObserver { NotificationCenter.default.removeObserver(clipboardObserver) }
        clipboardObserver=nil
        timer?.invalidate(); timer = nil; task?.cancel()
        if environmentOwner == nil {bridge.close();local?.close();local=nil}
        else if let terminalID {try? local?.closeTerminal(terminalID)}
        terminalID=nil;waylandWindows = []; onWaylandChanged = nil
        #if DEBUG
        onTerminalOutput=nil;onWaylandCommand=nil
        #endif
        pendingInput.removeAll()
        state = "已关闭"
    }
    /// Removing the first terminal view must retain its Linux/Wayland owner.
    /// Extra windows own only their PTY and can be disposed immediately.
    func closeTerminalWindow() {
        if environmentOwner != nil {close();return}
        guard !terminalClosed else {return};terminalClosed=true
        if let local,let terminalID {try? local.closeTerminal(terminalID);self.terminalID=nil}
        pendingInput.removeAll();state="已关闭"
    }
    func fail(_ error: Error) {
        guard !closed else { return }
        state = error.localizedDescription
    }
    func sizeChanged(source: TerminalView, newCols: Int, newRows: Int) {
        // SwiftTerm also delivers deferred size notifications. A completed
        // system-window resize must not be overwritten by an older snapshot.
        let grid=source.getTerminal()
        synchronizeTerminalSize(columns:grid.cols,rows:grid.rows)
    }
    private func synchronizeTerminalSize(columns:Int,rows:Int) {
        guard !terminalClosed,let local,let terminalID,columns>0,rows>0,terminalSize?.columns != columns || terminalSize?.rows != rows else {return}
        do { try local.resize(columns:columns,rows:rows,terminal:terminalID);terminalSize=(columns,rows) }
        catch { fail(error) }
    }
    func setTerminalTitle(source: TerminalView, title: String) {
        let value = String(title.prefix(80)).trimmingCharacters(in: .whitespacesAndNewlines)
        self.title = value.isEmpty ? fallbackTitle : value
    }
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
    let workspace: WorkspaceModel
    let onFocus: () -> Void
    func makeUIView(context: Context) -> TerminalViewportHost { TerminalViewportHost(terminal:session.terminal) }
    func sizeThatFits(_ proposal: ProposedViewSize, uiView: TerminalViewportHost, context: Context) -> CGSize? {
        guard let width=proposal.width,let height=proposal.height else { return nil }
        return CGSize(width:max(0,width),height:max(0,height))
    }
    func updateUIView(_ host: TerminalViewportHost, context: Context) {
        let view=host.terminal
        view.onFocusRequested = onFocus
        let id=WorkspaceItemID.terminal(session.id)
        // SwiftUI may deliver a snapshot queued before minimization. Read the
        // live model rather than focus/visibility captured in that snapshot.
        view.setKeyboardFocus(workspace.layout.focused == id,visible:workspace.isVisible(id),deferred:true)
    }
}
/// A plain UIKit viewport tracks system-window geometry even when SwiftUI does
/// not call updateUIView. Do not let the scroll view's content size become its
/// layout proposal during Stage Manager/full-screen transitions.
final class TerminalViewportHost:UIView {
    let terminal:TerminalViewport
    init(terminal:TerminalViewport) {
        self.terminal=terminal
        super.init(frame:.zero)
        clipsToBounds=true
        terminal.frame=bounds
        terminal.autoresizingMask=[.flexibleWidth,.flexibleHeight]
        addSubview(terminal)
    }
    required init?(coder:NSCoder) {fatalError("init(coder:) has not been implemented")}
    override func layoutSubviews() {
        super.layoutSubviews()
        if terminal.frame != bounds {terminal.frame=bounds;terminal.setNeedsLayout()}
        terminal.layoutIfNeeded()
    }
}
