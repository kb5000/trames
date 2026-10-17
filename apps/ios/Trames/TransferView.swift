// SPDX-License-Identifier: GPL-3.0-or-later
import SwiftUI
import UniformTypeIdentifiers
import TramesKit
struct ExportFile: Identifiable { let id = UUID(); let url: URL }
struct TransferView: View {
    @ObservedObject var session: TerminalSession
    @Environment(\.dismiss) private var dismiss
    @State private var path = "/root/imported-file"
    @State private var selected: URL?
    @State private var chooser = false
    @State private var busy = false
    @State private var replace = true
    @State private var message = ""
    @State private var exportFile: ExportFile?
    @State private var task: Task<Void,Never>?
    var body: some View {
        NavigationStack {
            Form {
                Section("本地 Linux 文件") {
                    Text(UserDefaults.standard.bool(forKey:"rootfsFileProviderAvailable") ? "文件 App 中可浏览 Trames › Alpine 的 rootfs（只读）。" : "当前签名未提供文件共享容器，可继续使用这里的导入和导出。")
                        .font(.caption).foregroundStyle(.secondary)
                    TextField("文件路径", text: $path).textInputAutocapitalization(.never).autocorrectionDisabled()
                    if let selected { Text(selected.lastPathComponent) }
                    Button("选择 Files 文件") { chooser = true }.disabled(busy)
                    Toggle("替换 Linux 中的同名文件",isOn:$replace).disabled(busy)
                    Button("导入到 Linux") { transfer(upload: true) }.disabled(busy || selected == nil)
                    Button("导出到 Files") { transfer(upload: false) }.disabled(busy)
                }
                Section { Text(message.isEmpty ? "导入使用文件副本；开启替换后，同名文件在传输完整成功后替换，取消或失败时保留原文件。" : message).font(.caption) }
                if busy { Button("取消传输", role: .destructive) { task?.cancel(); message = "取消中…" } }
            }.navigationTitle("Files")
                .toolbar { Button("完成") { task?.cancel(); dismiss() } }
                .fileImporter(isPresented: $chooser, allowedContentTypes: [.data, .item]) { result in
                    switch result {
                    case .success(let url): selected = url
                        if path == "/root/imported-file" { path = "/root/" + url.lastPathComponent }
                    case .failure(let error): message = error.localizedDescription
                    }
                }
                .sheet(item: $exportFile) { file in
                    FileExportPicker(url: file.url) { exportFile = nil; try? FileManager.default.removeItem(at: file.url) }
                }
                .onDisappear { task?.cancel() }
        }
    }
    private func transfer(upload: Bool) {
        guard !path.isEmpty else { message = "请输入文件路径"; return }
        busy = true; message = "传输中…"
        task = Task {
            let stage = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
            var keep = false
            defer { busy = false; if !keep { try? FileManager.default.removeItem(at: stage) } }
            do {
                if upload, let selected { try await ScopedFiles.stage(selected: selected, destination: stage) }
                try Task.checkCancellation()
                if let local = session.local {
                    try await local.transferAsync(guest: path, host: stage, export: !upload, replace:upload && replace)
                } else { throw TramesError.message("会话已关闭") }
                try Task.checkCancellation()
                if upload { message = "传输完成" }
                else { keep = true; exportFile = ExportFile(url: stage); message = "请选择导出位置" }
            } catch { message = error.localizedDescription }
        }
    }
}
struct FileExportPicker: UIViewControllerRepresentable {
    let url: URL
    let finished: () -> Void
    func makeCoordinator() -> Coordinator { Coordinator(finished: finished) }
    func makeUIViewController(context: Context) -> UIDocumentPickerViewController {
        let picker = UIDocumentPickerViewController(forExporting: [url], asCopy: true)
        picker.delegate = context.coordinator; return picker
    }
    func updateUIViewController(_ view: UIDocumentPickerViewController, context: Context) {}
    static func dismantleUIViewController(_ controller: UIDocumentPickerViewController, coordinator: Coordinator) { coordinator.cleanup() }
    final class Coordinator: NSObject, UIDocumentPickerDelegate {
        let finished: () -> Void
        private var done = false
        func cleanup() { guard !done else { return }; done = true; finished() }
        init(finished: @escaping () -> Void) { self.finished = finished }
        func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) { cleanup() }
        func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) { cleanup() }
    }
}
