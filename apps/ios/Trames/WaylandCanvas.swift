// SPDX-License-Identifier: GPL-3.0-or-later
import UIKit
import Metal
import QuartzCore
import TramesKit

/// Presentation and command failure can arrive on different Metal threads.
/// Complete the native lease/queue permit exactly once, at actual scanout (or
/// failed GPU completion), rather than treating GPU completion as presentation.
private final class WaylandPresentation: @unchecked Sendable {
    private let lock = NSLock()
    private var frame: LocalWaylandFrame?
    private var gpuFinished=false
    private var cancelled=false
    private let semaphore: DispatchSemaphore
    init(frame: LocalWaylandFrame, semaphore: DispatchSemaphore) {
        self.frame=frame;self.semaphore=semaphore
    }
    var isFinished: Bool {
        lock.lock();defer { lock.unlock() };return frame == nil
    }
    func cancel() {
        lock.lock();cancelled=true;let finished=gpuFinished;lock.unlock()
        if finished { finish(presented:false) }
    }
    func gpuCompleted(success: Bool) {
        lock.lock();gpuFinished=true;let discard=cancelled || !success;lock.unlock()
        if discard { finish(presented:false) }
    }
    func finish(presented: Bool) {
        lock.lock()
        let retained=frame;frame=nil
        lock.unlock()
        guard let retained else { return }
        retained.release(presented:presented);semaphore.signal()
    }
    deinit { finish(presented:false) }
}

@MainActor
final class WaylandCanvas: WaylandInputView {
    override class var layerClass: AnyClass { CAMetalLayer.self }
    private var metal: CAMetalLayer { layer as! CAMetalLayer }
    private let device: MTLDevice
    private let queue: MTLCommandQueue
    private let pipeline: MTLRenderPipelineState
    private let texturePipeline: MTLRenderPipelineState
    private let inFlight = DispatchSemaphore(value: 2)
    weak var session: LocalSession?
    var windowID: UInt64 = 0
    var presentationEnabled = true {
        didSet {
            guard oldValue != presentationEnabled else { return }
            displayLink?.isPaused = !presentationEnabled
            if presentationEnabled { lastRevision=nil;lastPresentationTime=nil }
            else { cancelPresentations() }
        }
    }
    var sceneOrigin: CGPoint = .zero { didSet { if oldValue != sceneOrigin { lastRevision=nil } } }
    var rendered: UInt64 = 0
    var copiedBytes: UInt64 = 0
    private(set) var completed: UInt64 = 0
    private(set) var presented: UInt64 = 0
    var presentationFeedbackAvailable: Bool {
        #if targetEnvironment(simulator)
        false
        #else
        true
        #endif
    }
    // Simulator SDK omits drawable presentation feedback. Keep its completion
    // fallback explicit; this counter is progress, never a claim of scanout.
    var frameProgress: UInt64 { presentationFeedbackAvailable ? presented : completed }
    private(set) var maximumPresentationInterval: CFTimeInterval = 0
    private var lastPresentationTime: CFTimeInterval?
    var lastRevision: UInt64?
    private var displayLink: CADisplayLink?
    private var presentations: [WaylandPresentation] = []
    private var foregroundObserver: NSObjectProtocol?
    private var pressedButton: Int?
    private var visibleRects: [CGRect] = []
    @MainActor private final class RefreshTarget: NSObject {
        weak var canvas: WaylandCanvas?
        init(_ canvas: WaylandCanvas) { self.canvas = canvas }
        @objc func tick() { canvas?.renderFrame() }
    }
    private var wantsKeyboardFocus = false
    func setKeyboardFocus(_ value: Bool) {
        guard wantsKeyboardFocus != value else { return }
        wantsKeyboardFocus = value
        if value && window != nil { _ = becomeFirstResponder() }
        else if !value && isFirstResponder { resignFirstResponder() }
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
        metal.allowsNextDrawableTimeout = true
        metal.presentsWithTransaction = false
        backgroundColor = .clear; isOpaque = false; metal.isOpaque = false; clipsToBounds = true
        isAccessibilityElement = true; accessibilityIdentifier = "wayland-canvas"
        addGestureRecognizer(UIHoverGestureRecognizer(target:self,action:#selector(hover(_:))))
        let scroll=UIPanGestureRecognizer(target:self,action:#selector(scroll(_:)))
        scroll.allowedTouchTypes=[NSNumber(value:UITouch.TouchType.indirectPointer.rawValue)]
        scroll.allowedScrollTypesMask = .all
        scroll.cancelsTouchesInView=false; scroll.delaysTouchesEnded=false; addGestureRecognizer(scroll)
        foregroundObserver = NotificationCenter.default.addObserver(forName:UIApplication.didBecomeActiveNotification,object:nil,queue:.main) { [weak self] _ in
            MainActor.assumeIsolated { self?.cancelPresentations();self?.lastRevision = nil }
        }
    }
    private func pointer(_ point:CGPoint) {
        sendInput?(["op":"pointer","window":windowID,"x":point.x+sceneOrigin.x,"y":point.y+sceneOrigin.y,"time":UInt32(truncatingIfNeeded:UInt64(ProcessInfo.processInfo.systemUptime*1000))])
    }
    override func point(inside point:CGPoint,with event:UIEvent?)->Bool {
        guard super.point(inside:point,with:event) else { return false }
        let location=CGPoint(x:point.x+sceneOrigin.x,y:point.y+sceneOrigin.y)
        // An outlying popup extends the canvas bounds; empty space between
        // surfaces must not intercept another native window's controls.
        return visibleRects.isEmpty || visibleRects.contains { $0.contains(location) }
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
        // Scroll-wheel/trackpad scrolling has no pressed pointer touch. A
        // primary-button drag already travels through touchesMoved/Ended.
        guard gesture.numberOfTouches == 0 else { return }
        let delta=gesture.translation(in:self);gesture.setTranslation(.zero,in:self)
        pointer(gesture.location(in:self))
        sendInput?(["op":"scroll","horizontal":-delta.x,"vertical":-delta.y,"time":inputTime])
    }
    private var inputTime:UInt32 { UInt32(truncatingIfNeeded:UInt64(ProcessInfo.processInfo.systemUptime*1000)) }
    required init?(coder: NSCoder) { fatalError("init(coder:) is unsupported") }
    override func caretRect(for position:UITextPosition)->CGRect {
        super.caretRect(for:position).offsetBy(dx:-sceneOrigin.x,dy:-sceneOrigin.y)
    }
    override func didMoveToWindow() {
        super.didMoveToWindow()
        displayLink?.invalidate(); displayLink = nil
        cancelPresentations()
        if window != nil {
            let link = CADisplayLink(target: RefreshTarget(self), selector: #selector(RefreshTarget.tick))
            let maximum=Float(window?.screen.maximumFramesPerSecond ?? 60)
            link.preferredFrameRateRange=CAFrameRateRange(minimum:min(30,maximum),maximum:maximum,preferred:maximum)
            link.isPaused = !presentationEnabled
            link.add(to: .main, forMode: .common); displayLink = link
            lastRevision = nil
            if wantsKeyboardFocus { _ = becomeFirstResponder() }
        }
    }
    deinit { if let foregroundObserver { NotificationCenter.default.removeObserver(foregroundObserver) } }
    override func layoutSubviews() {
        super.layoutSubviews()
        let scale=window?.screen.scale ?? 2
        let size=CGSize(width:max(1,bounds.width * scale),height:max(1,bounds.height * scale))
        if metal.contentsScale != scale || metal.drawableSize != size {
            metal.contentsScale=scale;metal.drawableSize=size;lastRevision=nil
        }
    }
    private func cancelPresentations() {
        presentations.forEach { $0.cancel() }
        presentations.removeAll { $0.isFinished }
    }
    private func renderFrame() {
        presentations.removeAll { $0.isFinished }
        guard presentationEnabled, bounds.width > 0, bounds.height > 0, window != nil, !isHidden,
              UIApplication.shared.applicationState == .active,
              inFlight.wait(timeout: .now()) == .success else { return }
        var submitted = false
        defer { if !submitted { inFlight.signal() } }
        do {
            guard let frame = try session?.waylandFrame(window: windowID) else { return }
            guard lastRevision != frame.info.revision else { return }
            visibleRects=frame.images.map { image in
                let scale=CGFloat(max(1,image.scale))
                let size=image.transform & 1 == 0 ? CGSize(width:CGFloat(image.width)/scale,height:CGFloat(image.height)/scale) : CGSize(width:CGFloat(image.height)/scale,height:CGFloat(image.width)/scale)
                return CGRect(origin:CGPoint(x:CGFloat(image.x),y:CGFloat(image.y)),size:size)
            }
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
                let uniform = ImageUniform(origin: SIMD2(Float(image.x)-Float(sceneOrigin.x),Float(image.y)-Float(sceneOrigin.y)),
                    size: image.transform & 1 == 0 ? SIMD2(Float(image.width)/scale,Float(image.height)/scale) : SIMD2(Float(image.height)/scale,Float(image.width)/scale),
                    viewport: SIMD2(Float(bounds.width),Float(bounds.height)), offset: UInt32(image.offset),
                    stride: image.stride,width: image.width,height: image.height,alpha: image.alpha,transform:image.transform)
                buffers.append((source,uniform))
            }
            guard let drawable = metal.nextDrawable(), let command = queue.makeCommandBuffer() else { return }
            let pass = MTLRenderPassDescriptor()
            pass.colorAttachments[0].texture = drawable.texture
            pass.colorAttachments[0].loadAction = .clear; pass.colorAttachments[0].storeAction = .store
            pass.colorAttachments[0].clearColor = MTLClearColorMake(0,0,0,0)
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
            let presentation=WaylandPresentation(frame:frame,semaphore:inFlight)
            presentations.append(presentation)
            let submittedRevision=frame.info.revision
            #if !targetEnvironment(simulator)
            drawable.addPresentedHandler { [weak self] drawable in
                let time=drawable.presentedTime
                presentation.finish(presented:time > 0)
                Task { @MainActor in
                    if time > 0 { self?.didPresent(at:time) }
                    else { self?.retryUnpresented(submittedRevision) }
                }
            }
            #endif
            command.addCompletedHandler { [weak self] result in
                let success=result.status == .completed
                presentation.gpuCompleted(success:success)
                #if targetEnvironment(simulator)
                // The simulator has no presented handler/presentedTime API.
                // Advance the protocol at GPU completion without inventing an
                // on-screen presentation count or timestamp.
                presentation.finish(presented:success)
                #endif
                Task { @MainActor in
                    if success { self?.didCompleteFrame() }
                    else { self?.retryUnpresented(submittedRevision) }
                }
            }
            command.present(drawable)
            command.commit(); submitted = true; lastRevision = frame.info.revision
            rendered += 1
            updatePresentationStatus()
        } catch { onError?(error.localizedDescription) }
    }
    private func retryUnpresented(_ revision: UInt64) {
        // Core Animation reports zero for skipped drawables. A dropped or
        // failed frame cannot leave FIFO waiting on a revision we never retry.
        if lastRevision == revision { lastRevision=nil }
    }
    private func didCompleteFrame() {
        completed += 1
        updatePresentationStatus()
    }
    private func didPresent(at time: CFTimeInterval) {
        presented += 1
        if let lastPresentationTime, time >= lastPresentationTime {
            maximumPresentationInterval=max(maximumPresentationInterval,time-lastPresentationTime)
        }
        lastPresentationTime=time
        updatePresentationStatus()
    }
    private func updatePresentationStatus() {
        let display=presentationFeedbackAvailable ? String(presented) : "unavailable"
        accessibilityValue="window=\(windowID) submitted=\(rendered) completed=\(completed) presented=\(display) maxIntervalMs=\(Int(maximumPresentationInterval*1000)) copied=\(copiedBytes)"
    }
}
