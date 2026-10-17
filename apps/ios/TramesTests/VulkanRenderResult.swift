// SPDX-License-Identifier: GPL-3.0-or-later
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
