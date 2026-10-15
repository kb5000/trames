// SPDX-License-Identifier: GPL-3.0-or-later
import SwiftUI
import Metal
import QuartzCore

@MainActor
final class VulkanCanvas: UIView {
    let metal = CAMetalLayer()
    let pixels: CGSize
    var frameCount: (() -> UInt64)?
    override var accessibilityValue: String? {
        get { "\(Int(pixels.width))x\(Int(pixels.height)) frames=\(frameCount?() ?? 0)" }
        set { super.accessibilityValue = newValue }
    }
    init(width: Int = 1024, height: Int = 768) {
        pixels = CGSize(width: width, height: height)
        super.init(frame: .zero)
        backgroundColor = .black
        isAccessibilityElement = true
        accessibilityIdentifier = "vulkan-display-canvas"
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
struct VulkanCanvasPane: UIViewRepresentable {
    let canvas: VulkanCanvas
    func makeUIView(context: Context) -> VulkanCanvas { canvas }
    func updateUIView(_ view: VulkanCanvas, context: Context) { view.setNeedsLayout() }
}
struct DisplayWindowGeometry {
    var origin = CGPoint(x: 32, y: 90)
    var size = CGSize(width: 600, height: 490)
    mutating func constrain(to viewport: CGSize) {
        size.width = min(max(240, size.width), max(1, viewport.width))
        size.height = min(max(200, size.height), max(1, viewport.height))
        origin.x = min(max(0, origin.x), max(0, viewport.width - size.width))
        origin.y = min(max(0, origin.y), max(0, viewport.height - size.height))
    }
}
struct VulkanDisplayWindow: View {
    @ObservedObject var session: TerminalSession
    let viewport: CGSize
    @State private var geometry = DisplayWindowGeometry()
    @State private var dragOrigin: CGPoint?
    @State private var resizeOrigin: CGSize?
    var body: some View {
        if session.displayEnabled {
        VStack(spacing: 0) {
            HStack {
                Text("显示器 · 1024 × 768").font(.callout).lineLimit(1)
                Spacer()
                Button { session.displayVisible = false } label: { Image(systemName: "minus") }
                    .accessibilityLabel("隐藏显示窗口")
            }.padding(.horizontal, 12).frame(height: 44).background(Color(white: 0.16))
                .accessibilityIdentifier("vulkan-display-title")
                .gesture(DragGesture().onChanged { value in
                    if dragOrigin == nil { dragOrigin = geometry.origin }
                    geometry.origin = CGPoint(x: dragOrigin!.x + value.translation.width, y: dragOrigin!.y + value.translation.height)
                    geometry.constrain(to: viewport)
                }.onEnded { _ in dragOrigin = nil })
            VulkanCanvasPane(canvas: session.displayCanvas)
                .accessibilityIdentifier("vulkan-display-canvas")
            HStack {
                Spacer()
                Image(systemName: "arrow.down.right.and.arrow.up.left").frame(width: 44, height: 32)
                    .contentShape(Rectangle()).accessibilityIdentifier("vulkan-display-resize")
                    .gesture(DragGesture().onChanged { value in
                        if resizeOrigin == nil { resizeOrigin = geometry.size }
                        geometry.size = CGSize(width: resizeOrigin!.width + value.translation.width, height: resizeOrigin!.height + value.translation.height)
                        geometry.constrain(to: viewport)
                    }.onEnded { _ in resizeOrigin = nil })
            }.background(Color(white: 0.16))
        }.frame(width: geometry.size.width, height: geometry.size.height)
            .background(.black).clipShape(RoundedRectangle(cornerRadius: 10))
            .overlay(RoundedRectangle(cornerRadius: 10).stroke(Color.white.opacity(0.3)))
            .shadow(radius: 14).offset(x: geometry.origin.x, y: geometry.origin.y)
            .opacity(session.displayVisible ? 1 : 0).allowsHitTesting(session.displayVisible)
            .onAppear { geometry.constrain(to: viewport) }
            .onChange(of: viewport) { _, value in geometry.constrain(to: value) }
        }
    }
}
