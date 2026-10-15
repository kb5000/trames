// SPDX-License-Identifier: GPL-3.0-or-later
import SwiftUI
import UIKit
import TramesKit

struct VulkanRenderResult {
    let text: String
    let image: UIImage
    init(text: String, ppm: Data) throws {
        // The bundled acceptance program writes a fixed, bounded P6 image.
        // Keep the exported GPU bytes; UIKit only displays, never draws the
        // triangle. The guest independently checked colors before exporting.
        let header = Data("P6\n256 192\n255\n".utf8)
        guard ppm.starts(with: header), ppm.count == header.count + 256 * 192 * 3,
              let provider = CGDataProvider(data: Data(ppm.dropFirst(header.count)) as CFData),
              let cgImage = CGImage(width: 256, height: 192, bitsPerComponent: 8,
                  bitsPerPixel: 24, bytesPerRow: 256 * 3, space: CGColorSpaceCreateDeviceRGB(),
                  bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.none.rawValue),
                  provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent) else {
            throw TramesError.message("GPU 输出不是完整的 256 × 192 图像")
        }
        self.text = text
        image = UIImage(cgImage: cgImage)
    }
}

struct VulkanRenderView: View {
    @Environment(\.dismiss) private var dismiss
    @State private var result: VulkanRenderResult?
    @State private var error: String?
    @State private var running = false
    @State private var iteration = UUID()
    var body: some View {
        NavigationStack {
            VStack(spacing: 20) {
                if let result {
                    Image(uiImage: result.image).resizable().interpolation(.none).aspectRatio(contentMode: .fit)
                        .accessibilityLabel("Vulkan GPU 渲染的彩色三角形")
                        .accessibilityIdentifier("gpu-render-image")
                    Text("256 × 192 · GPU 像素校验通过").font(.caption).foregroundStyle(.secondary)
                } else if running {
                    ProgressView("正在准备并渲染…")
                }
                if let error { Text(error).foregroundStyle(.red).textSelection(.enabled).accessibilityIdentifier("gpu-render-error") }
                Text("运行 Linux Vulkan 程序，由设备 GPU 绘制。示例使用临时环境，关闭后清理。").font(.callout).foregroundStyle(.secondary)
                Button("重新渲染") { iteration = UUID() }.disabled(running)
                Spacer(minLength: 0)
            }.padding(24).navigationTitle("GPU 渲染示例")
                .toolbar { Button("完成") { dismiss() } }
                .task(id: iteration) {
                    running = true; error = nil
                    defer { running = false }
                    do {
                        let frame = try await LocalVulkanProbe.render()
                        try Task.checkCancellation()
                        result = frame
                    } catch is CancellationError {
                    } catch { self.error = error.localizedDescription }
                }
        }
    }
}
