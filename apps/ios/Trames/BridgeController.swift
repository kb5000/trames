// SPDX-License-Identifier: GPL-3.0-or-later
import SwiftUI
import TramesKit
@MainActor
final class BridgeController: ObservableObject {
    @Published var challenge: BridgeHostRequest?
    private weak var local: LocalSession?
    var directories: ScopedDirectories?
    private var task: Task<Void,Never>?
    private var running: UInt64?
    func attach(_ session: LocalSession) { local = session }
    func poll() throws {
        guard let local else { return }
        if let challenge, !local.bridgeActive(challenge.ticket) { self.challenge = nil }
        if let running, !local.bridgeActive(running) { task?.cancel() }
        guard challenge == nil, running == nil, let request = try local.pollBridge() else { return }
        if UserDefaults.standard.object(forKey: "bridgeEnabled") as? Bool == false {
            try local.rejectBridge(request.ticket, error: BridgeFailure(code: "denied", message: "Native Bridge is disabled in settings"))
        } else { challenge = request }
    }
    func deny() {
        guard let request = challenge else { return }; challenge = nil
        try? local?.rejectBridge(request.ticket, error: BridgeFailure(code: "denied", message: "User denied this request"))
    }
    func allow() {
        guard let host = challenge, let local else { return }
        challenge = nil; running = host.ticket
        task = Task {
            defer { running = nil; task = nil }
            do {
                try Task.checkCancellation()
                guard local.bridgeActive(host.ticket) else { return }
                let result = try await execute(host.request, session: local)
                if local.bridgeActive(host.ticket) { try local.replyBridge(host.ticket, result: result) }
            } catch {
                let failure = error as? BridgeFailure ?? BridgeFailure(code: error is CancellationError ? "cancelled" : "provider_error", message: error is CancellationError ? "Request cancelled" : "Native operation failed")
                if local.bridgeActive(host.ticket) { try? local.rejectBridge(host.ticket, error: failure) }
            }
        }
    }
    func close() { task?.cancel(); challenge = nil; local = nil }
    private func execute(_ request: BridgeRequest, session: LocalSession) async throws -> BridgeValue {
        switch request.method {
        case "clipboard.read":
            try request.only([])
            let text = UIPasteboard.general.string ?? ""
            guard text.utf8.count <= 16384 else { throw BridgeFailure(code: "too_large", message: "Clipboard text exceeds 16 KiB") }
            return .object(["text": .string(text)])
        case "clipboard.write":
            try request.only(["text"])
            UIPasteboard.general.string = try request.string("text", maximum: 16384)
            return .object(["written": .bool(true)])
        case "url.open":
            try request.only(["url"])
            let url = try BridgeValidation.webURL(request.string("url"))
            let opened = await UIApplication.shared.open(url, options: [:])
            return .object(["opened": .bool(opened)])
        case "device.info":
            try request.only([])
            return .object(["platform": .string(UIDevice.current.systemName), "version": .string(UIDevice.current.systemVersion), "model": .string(UIDevice.current.model)])
        case "files.list":
            try request.only([])
            guard let directories else { throw BridgeFailure(code: "unavailable", message: "Files authorizations are unavailable") }
            return .object(["grants": .array(await directories.list().map { .object(["id": .string($0.id.uuidString), "name": .string($0.name), "writable": .bool($0.writable)]) })])
        case "files.import", "files.export":
            try request.only(["grant", "path", "guest"])
            guard let directories, let id = UUID(uuidString: try request.string("grant")) else { throw BridgeFailure(code: "invalid_params", message: "Expected a folder authorization ID") }
            try await directories.transfer(id: id, relative: request.string("path"), guest: request.string("guest"), export: request.method == "files.export", session: session)
            return .object(["copied": .bool(true)])
        default: throw BridgeFailure(code: "method_not_found", message: "Unknown native capability")
        }
    }
}
struct BridgeConsentPresenter: View {
    @ObservedObject var controller: BridgeController
    var body: some View {
        Color.clear.sheet(item: $controller.challenge) { host in
            NavigationStack {
                Form {
                    Section("本地 Linux 请求") {
                        Text(title(host.request.method)).font(.headline)
                        if let value = host.request.params["url"]?.text { Text(String(value.prefix(4096))).textSelection(.enabled) }
                        if let value = host.request.params["text"]?.text { Text(String(value.prefix(1024))).textSelection(.enabled) }
                        if let value = host.request.params["path"]?.text { LabeledContent("Files 路径", value: value) }
                        if let value = host.request.params["guest"]?.text { LabeledContent("Linux 路径", value: value) }
                        if let value = host.request.params["grant"]?.text { LabeledContent("目录授权", value: value) }
                    }
                    Section { Text("只允许这一次请求。任何 Linux 程序都可以发起请求，请核对操作内容。").font(.caption) }
                    Button("允许这一次") { controller.allow() }.accessibilityIdentifier("bridge-allow")
                    Button("拒绝", role: .cancel) { controller.deny() }.accessibilityIdentifier("bridge-deny")
                }.navigationTitle("允许 Linux 访问 iPad？").accessibilityIdentifier("bridge-consent")
            }.interactiveDismissDisabled()
        }
    }
    private func title(_ method: String) -> String {
        ["clipboard.read":"读取 iPad 剪贴板文本", "clipboard.write":"写入 iPad 剪贴板文本", "url.open":"在浏览器中打开链接", "device.info":"读取设备型号及系统版本", "files.list":"查看已授权的 Files 目录", "files.import":"从 Files 复制文件到 Linux", "files.export":"从 Linux 复制文件到 Files"][method] ?? method
    }
}
