// SPDX-License-Identifier: GPL-3.0-or-later
#if DEBUG
import UIKit
import Metal
import TramesKit

struct WaylandProbeResult {
    let text: String
    let png: Data
    let copiedBytes: UInt64
    let resized: Bool
    let textConfirmed: Bool
}
@MainActor
private final class WaylandPixelCapture {
    var bytes: Data?
    var width = 0
    var height = 0
    func encode(_ texture: MTLTexture, _ command: MTLCommandBuffer) {
        let width = texture.width, height = texture.height
        let row = (width * 4 + 255) & ~255
        guard let buffer = texture.device.makeBuffer(length: row * height, options:.storageModeShared),
              let blit = command.makeBlitCommandEncoder() else { return }
        blit.copy(from:texture, sourceSlice:0, sourceLevel:0, sourceOrigin:MTLOrigin(x:0,y:0,z:0),
                  sourceSize:MTLSize(width:width,height:height,depth:1), to:buffer,
                  destinationOffset:0, destinationBytesPerRow:row, destinationBytesPerImage:row * height)
        blit.endEncoding()
        command.addCompletedHandler { [weak self] result in
            guard result.status == .completed else { return }
            var pixels = Data(capacity:width * height * 4)
            for y in 0..<height { pixels.append(buffer.contents().advanced(by:y * row).assumingMemoryBound(to:UInt8.self), count:width * 4) }
            Task { @MainActor in self?.bytes=pixels; self?.width=width; self?.height=height }
        }
    }
    func png() throws -> Data {
        guard let bytes, let provider=CGDataProvider(data:bytes as CFData),
              let image=CGImage(width:width,height:height,bitsPerComponent:8,bitsPerPixel:32,bytesPerRow:width * 4,
                space:CGColorSpaceCreateDeviceRGB(),bitmapInfo:[.byteOrder32Little,CGBitmapInfo(rawValue:CGImageAlphaInfo.premultipliedFirst.rawValue)],
                provider:provider,decode:nil,shouldInterpolate:false,intent:.defaultIntent),
              let png=UIImage(cgImage:image).pngData() else { throw TramesError.message("Missing native Wayland drawable pixels") }
        return png
    }
}
/// Original guest applications plus actual CAMetalLayer drawables. Readback is
/// confined to this debug acceptance probe, never used for presentation.
@MainActor
enum LocalWaylandProbe {
    private static func repositoryFiles(_ repository: URL) throws -> [URL] {
        guard let entries = FileManager.default.enumerator(at: repository, includingPropertiesForKeys: [.isRegularFileKey]) else {
            throw TramesError.message("Missing offline Wayland repository")
        }
        return try entries.compactMap { $0 as? URL }.filter { try $0.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile == true }
    }
    static func run() async throws -> WaylandProbeResult {
        guard let resources=Bundle.main.resourceURL?.appendingPathComponent("GuestVulkan"),
              let icd=Bundle.main.url(forResource:"libtrames_vulkan_icd",withExtension:"so",subdirectory:"GuestVulkan") else { throw TramesError.message("Missing Wayland acceptance inputs") }
        let inputs=resources.appendingPathComponent("wayland-acceptance")
        let directory=FileManager.default.temporaryDirectory.appendingPathComponent("wayland-\(UUID().uuidString)",isDirectory:true)
        try FileManager.default.createDirectory(at:directory,withIntermediateDirectories:true)
        defer { try? FileManager.default.removeItem(at:directory) }
        let store=directory.appendingPathComponent("store",isDirectory:true)
        try LocalSession.install(store:store,rootfs:resources.appendingPathComponent("acceptance/alpine-minirootfs.tar.gz"),total:2<<30,available:2<<30)
        let session=try LocalSession(store:store,rootfs:nil,total:2<<30,available:2<<30,network:"off",vulkanICD:icd)
        defer { session.close() }
        let previous=UIApplication.shared.connectedScenes.compactMap{$0 as? UIWindowScene}.flatMap(\.windows).first{$0.isKeyWindow}
        guard let scene=previous?.windowScene else { throw TramesError.message("Missing Wayland test scene") }
        let window=UIWindow(windowScene:scene), controller=UIViewController()
        let canvas=try WaylandCanvas(), second=try WaylandCanvas(), capture=WaylandPixelCapture()
        canvas.session=session; second.session=session
        canvas.frame=CGRect(x:0,y:0,width:320,height:240); second.frame=CGRect(x:340,y:0,width:160,height:120)
        canvas.enablePixelInspection(); canvas.inspectFrame={ [weak capture] texture,command in capture?.encode(texture,command) }
        canvas.sendInput={try? session.waylandCommand($0)}
        var renderingError:String?
        canvas.onError={ renderingError=$0 }; second.onError={ renderingError=$0 }
        controller.view.backgroundColor = .black
        controller.view.addSubview(canvas); controller.view.addSubview(second)
        window.rootViewController=controller; window.makeKeyAndVisible()
        defer { window.isHidden=true; previous?.makeKeyAndVisible() }
        var text=""
        func receive() throws {
            text += String(decoding:try session.receive(),as:UTF8.self)
            let status=try session.status()
            if let error=renderingError { throw TramesError.message(error) }
            if status.phase != "running" && status.phase != "starting" { throw TramesError.message(status.error ?? "Wayland session stopped: \(text.suffix(4096))") }
        }
        func send(_ command:String) throws {
            let data=Data(command.utf8)
            guard try session.send(data)==data.count else { throw TramesError.message("Wayland acceptance input queue full") }
        }
        let began=Date()
        while try session.status().phase == "starting" {
            if Date().timeIntervalSince(began)>60 { throw TramesError.message("Wayland session did not start") }
            try await Task.sleep(for:.milliseconds(10))
        }
        let repository=inputs.appendingPathComponent("repository",isDirectory:true)
        for file in try repositoryFiles(repository) {
            let relative=String(file.path.dropFirst(repository.path.count+1))
            try session.transfer(guest:"/tmp/wayland-repo/"+relative,host:file,export:false)
        }
        for (guest,host) in [("wayland-client","wayland-client"),("weston-assets.apk","weston-assets.apk.input"),("wayland-assets.tar","assets.tar"),("wayland-repositories","repositories")] {
            try session.transfer(guest:"/tmp/"+guest,host:inputs.appendingPathComponent(host),export:false)
        }
        try send("apk --repositories-file /tmp/wayland-repositories --no-progress add wayland-utils weston-clients vulkan-tools vulkan-loader vkmark font-dejavu breeze-cursors && apk verify /tmp/weston-assets.apk && tar -xf /tmp/wayland-assets.tar -C / && chmod 755 /tmp/wayland-client; printf '\\nWAYLAND_SETUP:%d\\n' $?\n")
        while !text.contains("\r\nWAYLAND_SETUP:0\r\n") {
            try receive()
            if text.range(of:"\r\nWAYLAND_SETUP:[1-9][0-9]*\r\n",options:.regularExpression) != nil || Date().timeIntervalSince(began)>300 { throw TramesError.message("Wayland package setup failed: \(text.suffix(4096))") }
            try await Task.sleep(for:.milliseconds(10))
        }
        var cubeResized=false, textConfirmed=false
        for (stage,program) in [("info","wayland-info"),("windows","/tmp/wayland-client 1 hold"),("shm","weston-simple-shm"),("editor","weston-editor"),("cube","vkcube --wsi wayland --c 30"),("vkmark","vkmark --winsys wayland --size 320x240 -b clear:duration=0.1 -b texture:anisotropy=16:duration=0.1 -b shading:shading=phong:duration=0.1")] {
            let start=Date(), baseline=canvas.rendered
            var focused=false, resized=false, closed=false
            var currentID:UInt64?
            var editorStep=0
            var editorFirst:UInt64?
            canvas.windowID=0; second.windowID=0; canvas.lastRevision=nil; second.lastRevision=nil
            try send("XDG_RUNTIME_DIR=/run/user/0 WAYLAND_DISPLAY=wayland-0 VK_DRIVER_FILES=/usr/share/vulkan/icd.d/trames.json \(program); printf '\\nWAYLAND_RESULT:\(stage):%d\\n' $?\n")
            let marker="\r\nWAYLAND_RESULT:\(stage):0\r\n"
            while !text.contains(marker) {
                try receive()
                if text.range(of:"\r\nWAYLAND_RESULT:\(stage):[1-9][0-9]*\r\n",options:.regularExpression) != nil {
                    try send("dmesg; printf '\\nWAYLAND_KERNEL_LOG_END\\n'\n")
                    let deadline=Date().addingTimeInterval(3)
                    while !text.contains("\r\nWAYLAND_KERNEL_LOG_END\r\n") && Date()<deadline {
                        try receive()
                        try await Task.sleep(for:.milliseconds(10))
                    }
                    throw TramesError.message("Wayland \(stage) failed: \(text.suffix(16384))")
                }
                let status=try session.waylandStatus()
                if let first=status.windows.first {
                    currentID=first.id; canvas.windowID=first.id
                    second.windowID=status.windows.dropFirst().first?.id ?? 0
                    canvas.textContext=status.text?.window==first.id ? status.text : nil
                    if !focused {
                        try session.waylandCommand(["op":"focus","window":first.id]); focused=true
                        if stage=="editor" {
                            for command:[String:Any] in [["op":"pointer","window":first.id,"x":80,"y":80,"time":1],
                                ["op":"button","button":0x110,"pressed":true,"time":2], ["op":"button","button":0x110,"pressed":false,"time":3]] { try session.waylandCommand(command) }
                        }
                    }
                    if (stage=="windows" || stage=="cube") && !resized && canvas.rendered>baseline {
                        try session.waylandCommand(["op":"resize","window":first.id,"width":320,"height":240]); resized=true
                    }
                    let sizeOK=first.width==320 && first.height==240
                    if stage=="cube" { cubeResized = cubeResized || resized && sizeOK }
                    if stage=="editor", let context=status.text {
                        canvas.textContext=context
                        if editorStep == 0 {
                            editorFirst=context.id
                            canvas.setMarkedText("ni",selectedRange:NSRange(location:2,length:0));canvas.insertText("你好🙂");editorStep=1
                        } else if editorStep == 1 && context.text.contains("你好🙂") {
                            for command:[String:Any] in [["op":"pointer","window":first.id,"x":80,"y":Double(first.height)*0.75,"time":4],
                                ["op":"button","button":0x110,"pressed":true,"time":5],["op":"button","button":0x110,"pressed":false,"time":6]] { try session.waylandCommand(command) }
                            editorStep=2
                        } else if editorStep == 2 && context.id != editorFirst {
                            canvas.insertText("第二框");editorStep=3
                        } else if editorStep == 3 && context.text.contains("第二框") {
                            for command:[String:Any] in [["op":"pointer","window":first.id,"x":80,"y":80,"time":7],
                                ["op":"button","button":0x110,"pressed":true,"time":8],["op":"button","button":0x110,"pressed":false,"time":9]] { try session.waylandCommand(command) }
                            editorStep=4
                        } else if editorStep == 4 && context.id == editorFirst && context.text.contains("你好🙂") {
                            canvas.insertText("返回");editorStep=5
                        } else if editorStep == 5 && context.text.contains("返回") { textConfirmed=true }
                    }
                    let complete = stage=="windows" && resized && sizeOK && status.windows.count==2 && canvas.rendered>=baseline+2
                        || stage=="shm" && canvas.rendered>=baseline+4 || stage=="editor" && textConfirmed
                    if complete && !closed { try session.waylandCommand(["op":"close","window":first.id]); closed=true }
                }
                if Date().timeIntervalSince(start)>180 { throw TramesError.message("Wayland \(stage) timed out (window=\(String(describing:currentID)), frames=\(canvas.rendered-baseline)): \(text.suffix(4096))") }
                try await Task.sleep(for:.milliseconds(10))
            }
            if stage == "vkmark" && (!text.contains("vkmark Score") || text.contains("Failed with exception") || canvas.rendered == baseline) {
                throw TramesError.message("Original vkmark scene failed: \(text.suffix(16384))")
            }
            while !(try session.waylandStatus()).windows.isEmpty {
                if Date().timeIntervalSince(start)>185 { throw TramesError.message("Wayland client resources were not reclaimed") }
                try await Task.sleep(for:.milliseconds(10))
            }
        }
        try await Task.sleep(for:.milliseconds(50))
        guard let pixels=capture.bytes else { throw TramesError.message("Wayland native canvas produced no pixels") }
        var colors=Set<UInt32>()
        pixels.withUnsafeBytes { raw in
            let p=raw.bindMemory(to:UInt8.self)
            for at in stride(from:0,to:p.count,by:4) { colors.insert(UInt32(p[at]) | UInt32(p[at+1])<<8 | UInt32(p[at+2])<<16) }
        }
        guard colors.count>64, cubeResized, textConfirmed else { throw TramesError.message("Wayland acceptance incomplete: colors=\(colors.count), resize=\(cubeResized), IME=\(textConfirmed)") }
        return WaylandProbeResult(text:text,png:try capture.png(),copiedBytes:canvas.copiedBytes+second.copiedBytes,resized:cubeResized,textConfirmed:textConfirmed)
    }
}
#endif
