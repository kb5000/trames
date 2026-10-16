// SPDX-License-Identifier: GPL-3.0-or-later
import UIKit
import Metal
import QuartzCore
import TramesKit

@MainActor
final class WaylandCanvas: WaylandInputView {
    override class var layerClass: AnyClass { CAMetalLayer.self }
    private var metal: CAMetalLayer { layer as! CAMetalLayer }
    private let device: MTLDevice
    private let queue: MTLCommandQueue
    private let pipeline: MTLRenderPipelineState
    private let texturePipeline: MTLRenderPipelineState
    private let inFlight = DispatchSemaphore(value: 3)
    weak var session: LocalSession?
    var windowID: UInt64 = 0
    var rendered: UInt64 = 0
    var copiedBytes: UInt64 = 0
    var lastRevision: UInt64?
    private var displayLink: CADisplayLink?
    private var foregroundObserver: NSObjectProtocol?
    private var pressedButton: Int?
    private final class RefreshTarget: NSObject {
        weak var canvas: WaylandCanvas?
        init(_ canvas: WaylandCanvas) { self.canvas = canvas }
        @objc func tick() { canvas?.renderFrame() }
    }
    var revision: UInt64 = 0
    var onError: ((String) -> Void)?
    #if DEBUG
    // Test-only readback of the completed host drawable. Production GPU images
    // are imported directly and the CAMetalLayer remains framebufferOnly.
    var inspectFrame: ((MTLTexture, MTLCommandBuffer) -> Void)?
    func enablePixelInspection() { metal.framebufferOnly = false }
    #endif
    private enum Source { case shared(MTLBuffer), texture(MTLTexture) }
    private struct ImageUniform {
        var origin: SIMD2<Float>
        var size: SIMD2<Float>
        var viewport: SIMD2<Float>
        var offset: UInt32
        var stride: UInt32
        var width: UInt32
        var height: UInt32
        var alpha: UInt32
        var transform: UInt32
    }
    init(sharedDevice: MTLDevice? = MTLCreateSystemDefaultDevice()) throws {
        guard let device = sharedDevice, let queue = device.makeCommandQueue(),
              let library = device.makeDefaultLibrary() else { throw TramesError.message("Metal compositor unavailable") }
        self.device = device; self.queue = queue
        let descriptor = MTLRenderPipelineDescriptor()
        descriptor.vertexFunction = library.makeFunction(name: "waylandVertex")
        descriptor.fragmentFunction = library.makeFunction(name: "waylandShm")
        let color = descriptor.colorAttachments[0]!
        color.pixelFormat = .bgra8Unorm
        color.isBlendingEnabled = true
        color.sourceRGBBlendFactor = .one; color.destinationRGBBlendFactor = .oneMinusSourceAlpha
        color.sourceAlphaBlendFactor = .one; color.destinationAlphaBlendFactor = .oneMinusSourceAlpha
        pipeline = try device.makeRenderPipelineState(descriptor: descriptor)
        descriptor.fragmentFunction = library.makeFunction(name:"waylandTexture")
        texturePipeline = try device.makeRenderPipelineState(descriptor:descriptor)
        super.init(frame: .zero)
        metal.device = device; metal.pixelFormat = .bgra8Unorm
        metal.framebufferOnly = true; metal.maximumDrawableCount = 3
        metal.presentsWithTransaction = false
        backgroundColor = .black; clipsToBounds = true
        isAccessibilityElement = true; accessibilityIdentifier = "wayland-canvas"
        addGestureRecognizer(UIHoverGestureRecognizer(target:self,action:#selector(hover(_:))))
        let scroll=UIPanGestureRecognizer(target:self,action:#selector(scroll(_:)))
        scroll.allowedTouchTypes=[NSNumber(value:UITouch.TouchType.indirectPointer.rawValue)]
        scroll.allowedScrollTypesMask = .all
        scroll.cancelsTouchesInView=false; addGestureRecognizer(scroll)
        foregroundObserver = NotificationCenter.default.addObserver(forName:UIApplication.didBecomeActiveNotification,object:nil,queue:.main) { [weak self] _ in
            MainActor.assumeIsolated { self?.lastRevision = nil }
        }
    }
    private func pointer(_ point:CGPoint) {
        sendInput?(["op":"pointer","window":windowID,"x":point.x,"y":point.y,"time":UInt32(truncatingIfNeeded:UInt64(ProcessInfo.processInfo.systemUptime*1000))])
    }
    override func touchesBegan(_ touches:Set<UITouch>,with event:UIEvent?) {
        guard pressedButton == nil, let touch=touches.first else { return }
        onFocus?();pointer(touch.location(in:self));becomeFirstResponder()
        let button = event?.buttonMask.contains(.secondary) == true ? 0x111 : 0x110
        pressedButton=button
        sendInput?(["op":"button","button":button,"pressed":true,"time":UInt32(truncatingIfNeeded:UInt64(touch.timestamp*1000))])
    }
    override func touchesMoved(_ touches:Set<UITouch>,with event:UIEvent?) { if let touch=touches.first {pointer(touch.location(in:self))} }
    override func touchesEnded(_ touches:Set<UITouch>,with event:UIEvent?) {
        if let touch=touches.first, let button=pressedButton { pointer(touch.location(in:self));sendInput?(["op":"button","button":button,"pressed":false,"time":UInt32(truncatingIfNeeded:UInt64(touch.timestamp*1000))]);pressedButton=nil }
    }
    override func touchesCancelled(_ touches:Set<UITouch>,with event:UIEvent?) { touchesEnded(touches,with:event) }
    @objc private func hover(_ gesture:UIHoverGestureRecognizer) {
        if gesture.state == .ended || gesture.state == .cancelled {
            if pressedButton == nil { sendInput?(["op":"pointer","window":0,"x":0,"y":0,"time":inputTime]) }
        } else { pointer(gesture.location(in:self)) }
    }
    @objc private func scroll(_ gesture:UIPanGestureRecognizer) {
        let delta=gesture.translation(in:self);gesture.setTranslation(.zero,in:self)
        pointer(gesture.location(in:self))
        sendInput?(["op":"scroll","horizontal":-delta.x,"vertical":-delta.y,"time":inputTime])
    }
    private var inputTime:UInt32 { UInt32(truncatingIfNeeded:UInt64(ProcessInfo.processInfo.systemUptime*1000)) }
    required init?(coder: NSCoder) { fatalError("init(coder:) is unsupported") }
    override func didMoveToWindow() {
        super.didMoveToWindow()
        displayLink?.invalidate(); displayLink = nil
        if window != nil {
            let link = CADisplayLink(target: RefreshTarget(self), selector: #selector(RefreshTarget.tick))
            link.add(to: .main, forMode: .common); displayLink = link
            lastRevision = nil
        }
    }
    deinit { if let foregroundObserver { NotificationCenter.default.removeObserver(foregroundObserver) } }
    override func layoutSubviews() {
        super.layoutSubviews()
        metal.contentsScale = window?.screen.scale ?? 2
        metal.drawableSize = CGSize(width: max(1,bounds.width * metal.contentsScale), height: max(1,bounds.height * metal.contentsScale))
        lastRevision = nil
    }
    private func renderFrame() {
        guard bounds.width > 0, bounds.height > 0, window != nil, !isHidden,
              UIApplication.shared.applicationState == .active,
              inFlight.wait(timeout: .now()) == .success else { return }
        var submitted = false
        defer { if !submitted { inFlight.signal() } }
        do {
            guard let frame = try session?.waylandFrame(window: windowID) else { return }
            guard lastRevision != frame.info.revision else { return }
            var buffers: [(Source, ImageUniform)] = []
            for image in frame.images {
                guard let pointer = UnsafeMutableRawPointer(bitPattern: UInt(image.address)) else { continue }
                let source:Source
                if image.kind == 1 {
                    guard image.length > 0, image.offset <= UInt64(UInt32.max) else { continue }
                    // The Rust frame retains the full page-aligned mmap. The
                    // shader handles arbitrary row stride without image copies.
                    if let buffer = device.makeBuffer(bytesNoCopy:pointer,length:Int(image.length),options:.storageModeShared,deallocator:nil) {
                        source = .shared(buffer)
                    } else if let copy = device.makeBuffer(bytes:pointer,length:Int(image.length),options:.storageModeShared) {
                        source = .shared(copy); copiedBytes += image.length
                    } else { throw TramesError.message("Metal shared-buffer import failed") }
                } else if image.kind == 2 {
                    // Producer fence readiness and the native frame lease make
                    // this original MoltenVK-exported Metal texture live here.
                    guard let texture = Unmanaged<AnyObject>.fromOpaque(pointer).takeUnretainedValue() as? MTLTexture else {
                        throw TramesError.message("Invalid native GPU image")
                    }
                    source = .texture(texture)
                } else { continue }
                let scale = Float(max(1,image.scale))
                let uniform = ImageUniform(origin: SIMD2(Float(image.x),Float(image.y)),
                    size: image.transform & 1 == 0 ? SIMD2(Float(image.width)/scale,Float(image.height)/scale) : SIMD2(Float(image.height)/scale,Float(image.width)/scale),
                    viewport: SIMD2(Float(bounds.width),Float(bounds.height)), offset: UInt32(image.offset),
                    stride: image.stride,width: image.width,height: image.height,alpha: image.alpha,transform:image.transform)
                buffers.append((source,uniform))
            }
            guard let drawable = metal.nextDrawable(), let command = queue.makeCommandBuffer() else { return }
            let pass = MTLRenderPassDescriptor()
            pass.colorAttachments[0].texture = drawable.texture
            pass.colorAttachments[0].loadAction = .clear; pass.colorAttachments[0].storeAction = .store
            pass.colorAttachments[0].clearColor = MTLClearColorMake(0,0,0,1)
            guard let encoder = command.makeRenderCommandEncoder(descriptor: pass) else { return }
            for (source, value) in buffers {
                var uniform = value
                encoder.setVertexBytes(&uniform,length: MemoryLayout<ImageUniform>.stride,index: 0)
                switch source {
                case .shared(let buffer):
                    encoder.setRenderPipelineState(pipeline)
                    encoder.setFragmentBuffer(buffer,offset:0,index:0)
                case .texture(let texture):
                    encoder.setRenderPipelineState(texturePipeline)
                    encoder.setFragmentTexture(texture,index:0)
                }
                encoder.setFragmentBytes(&uniform,length: MemoryLayout<ImageUniform>.stride,index: 1)
                encoder.drawPrimitives(type: .triangleStrip,vertexStart: 0,vertexCount: 4)
            }
            encoder.endEncoding()
            #if DEBUG
            inspectFrame?(drawable.texture, command)
            #endif
            command.present(drawable)
            let semaphore = inFlight
            command.addCompletedHandler { result in
                // The driver retains MTLBuffers; frame owns their mmap bytes.
                frame.release(presented: result.status == .completed)
                semaphore.signal()
            }
            command.commit(); submitted = true; lastRevision = frame.info.revision
            rendered += 1
            accessibilityValue = "window=\(windowID) frames=\(rendered) copied=\(copiedBytes)"
        } catch { onError?(error.localizedDescription) }
    }
}
