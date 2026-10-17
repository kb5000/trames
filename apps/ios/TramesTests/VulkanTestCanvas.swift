// SPDX-License-Identifier: GPL-3.0-or-later
// Native display WSI test target only. The app presents graphical clients via Wayland.
import UIKit
import Metal
import QuartzCore

@MainActor
final class VulkanCanvas: UIView {
    let metal = CAMetalLayer()
    let pixels: CGSize
    init(width: Int = 1024, height: Int = 768) {
        pixels = CGSize(width: width, height: height)
        super.init(frame: .zero)
        backgroundColor = .black
        metal.device = MTLCreateSystemDefaultDevice()
        metal.pixelFormat = .bgra8Unorm
        metal.framebufferOnly = false
        metal.contentsScale = 1
        metal.bounds = CGRect(origin: .zero, size: pixels)
        metal.drawableSize = pixels
        metal.maximumDrawableCount = 3
        metal.presentsWithTransaction = false
        layer.addSublayer(metal)
        clipsToBounds = true
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) is unsupported") }
    override func layoutSubviews() {
        super.layoutSubviews()
        // Surface capabilities see constant pixel bounds. Only its parent
        // presentation transform changes, so window resizing never recreates
        // guest swapchains or changes the render target resolution.
        let scale = min(bounds.width / pixels.width, bounds.height / pixels.height)
        CATransaction.begin(); CATransaction.setDisableActions(true)
        metal.position = CGPoint(x: bounds.midX, y: bounds.midY)
        metal.setAffineTransform(CGAffineTransform(scaleX: scale, y: scale))
        CATransaction.commit()
    }
}
