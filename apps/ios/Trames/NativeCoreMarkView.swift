// SPDX-License-Identifier: GPL-3.0-or-later
import SwiftUI
import UIKit
import Darwin
import CCoreMark

@MainActor
final class NativeCoreMarkRunner: ObservableObject {
    @Published private(set) var running = false
    @Published private(set) var output = ""
    @Published private(set) var status = "尚未运行"
    private var interrupted = false

    nonisolated static func execute(iterations: UInt32 = 0, validation: Bool = false) -> (Int32, String) {
        var buffer = [CChar](repeating: 0, count: 16384)
        let capacity = buffer.count
        let code = trames_coremark_run(iterations, validation ? 1 : 0, &buffer, capacity)
        return (code, String(cString: buffer))
    }

    func markInterrupted() {
        if running { interrupted = true }
    }

    func start(workspace: WorkspaceModel, validation: Bool) {
        guard !running else { return }
        running = true
        interrupted = false
        status = "正在校准与计分，请保持前台…"
        let info = ProcessInfo.processInfo
        let started = Date()
        let wasLowPower = info.isLowPowerModeEnabled
        let appVersion = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "?"
        let appBuild = Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "?"
        let previousIdleTimer = UIApplication.shared.isIdleTimerDisabled
        UIApplication.shared.isIdleTimerDisabled = true
        output = "Trames 原生 CoreMark（临时测试包）\n" +
            "App：\(appVersion) build \(appBuild)\n" +
            "上游：eembc/coremark @ 1f483d5b8316753a742cbf5590caf5bd0a4e4777\n" +
            "开始：\(started.ISO8601Format())\n" +
            "系统：\(UIDevice.current.systemName) \(UIDevice.current.systemVersion)\n" +
            "设备型号：\(Self.hardwareModel())\n" +
            "线程：1；总数据：2000 bytes；seeds：\(validation ? "0x3415 0x3415 0x66" : "0 0 0x66")\n" +
            "低电量模式：\(wasLowPower)；热状态：\(info.thermalState.rawValue)\n" +
            "原生 ARM64 C；自动迭代；CLOCK_MONOTONIC；Linux 暂停\n\n"
        #if DEBUG
        output += "当前为 Debug / Simulator 测试构建，请使用真机 Release IPA 比较性能。\n\n"
        #endif
        workspace.setBenchmarkRunning(true)
        Task {
            // Let the guest worker observe its pause before calibration.
            try? await Task.sleep(for: .milliseconds(250))
            let (code, report) = await Task.detached(priority: .userInitiated) {
                Self.execute(validation: validation)
            }.value
            output += report + "\n结束热状态：\(info.thermalState.rawValue)\n"
            if interrupted {
                output += "本次进入后台或失去活动状态，结果仅供诊断，请重新运行。\n"
                status = "测试被打断，请重新运行"
            } else if code == 0 && report.contains("Correct operation validated") {
                status = "完成 · CRC 校验通过 · 计分至少 10 秒"
            } else {
                status = "结果未通过验收（状态 \(code)），请查看输出"
            }
            if wasLowPower || info.isLowPowerModeEnabled {
                output += "本次启用了低电量模式；建议关闭后再比较。\n"
            }
            UIApplication.shared.isIdleTimerDisabled = previousIdleTimer
            workspace.setBenchmarkRunning(false)
            running = false
        }
    }

    private static func hardwareModel() -> String {
        var system = utsname()
        uname(&system)
        return withUnsafeBytes(of: &system.machine) { bytes in
            String(decoding: bytes.prefix { $0 != 0 }, as: UTF8.self)
        }
    }
}

struct NativeCoreMarkView: View {
    @ObservedObject var runner: NativeCoreMarkRunner
    let workspace: WorkspaceModel
    @State private var validation = false
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            VStack(alignment: .leading, spacing: 12) {
                Text("直接在 iPad 上运行官方 CoreMark C 代码。单线程，-O2，无 LTO；自动校准后计分至少 10 秒，总耗时可能超过一分钟。运行时暂停本地 Linux。").font(.callout)
                Toggle("使用 validation seeds", isOn: $validation).disabled(runner.running)
                HStack {
                    Button(runner.running ? "运行中…" : "运行原生 CoreMark") {
                        runner.start(workspace: workspace, validation: validation)
                    }.buttonStyle(.borderedProminent).disabled(runner.running)
                        .accessibilityIdentifier("native-coremark-run")
                    if runner.running { ProgressView() }
                    Spacer()
                    if !runner.output.isEmpty && !runner.running {
                        ShareLink(item: runner.output) { Label("分享结果", systemImage: "square.and.arrow.up") }
                        Button("复制") { UIPasteboard.general.string = runner.output }
                    }
                }
                Text(runner.status).font(.callout).accessibilityIdentifier("native-coremark-status")
                ScrollView {
                    Text(runner.output).font(.system(.caption, design: .monospaced))
                        .textSelection(.enabled).frame(maxWidth: .infinity, alignment: .leading)
                }
                Text("与 guest 的 Iterations/Sec 比较；默认 make PORT_DIR=linux 也是单线程 -O2。Apple Clang 与 guest GCC 的代码生成仍有差异。请保持前台、关闭低电量模式，并等待设备冷却。").font(.caption).foregroundStyle(.secondary)
            }.padding().navigationTitle("原生 CoreMark")
                .toolbar { Button("完成") { dismiss() }.disabled(runner.running) }
                .interactiveDismissDisabled(runner.running)
        }
    }
}
