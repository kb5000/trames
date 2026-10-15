// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
import TramesKit
import UIKit

@MainActor
enum LocalVulkanProbe {
    static func run() async throws -> String { try await execute(render: false).text }
    static func render() async throws -> VulkanRenderResult {
        let result = try await execute(render: true)
        guard let ppm = result.ppm, result.text.contains("vulkan-render-ok"), result.text.contains("\r\nVULKAN_RESULT:0\r\n") else {
            throw TramesError.message("GPU 渲染未完成：\(result.text.suffix(2048))")
        }
        return try VulkanRenderResult(text: result.text, ppm: ppm)
    }
    static func present() async throws -> String {
        let previous = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.flatMap(\.windows).first { $0.isKeyWindow }
        guard let scene = previous?.windowScene else { throw TramesError.message("Missing test window scene") }
        let window = UIWindow(windowScene: scene)
        let controller = UIViewController()
        let canvas = VulkanCanvas()
        window.rootViewController = controller
        canvas.frame = window.bounds
        canvas.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        controller.view.addSubview(canvas)
        window.makeKeyAndVisible()
        defer { window.isHidden = true; previous?.makeKeyAndVisible() }
        return try await execute(render: false, display: canvas).text
    }
    // Test-only fixture launch. Production programs use the ordinary terminal.
    #if DEBUG
    static func submitPresentation(to session: LocalSession) async throws {
        guard let inputs = Bundle.main.resourceURL?.appendingPathComponent("GuestVulkan/acceptance") else {
            throw TramesError.message("Missing presentation fixture")
        }
        let began = Date()
        while try session.status().phase == "starting" {
            if Date().timeIntervalSince(began) > 60 { throw TramesError.message("Display session did not start") }
            try await Task.sleep(for: .milliseconds(10))
        }
        try prepare(session, inputs: inputs, program: "vulkan-present", arguments: "/tmp/vulkan-render.vert.spv /tmp/vulkan-render.frag.spv continuous")
    }
    #endif
    private static func prepare(_ session: LocalSession, inputs: URL, program: String, arguments: String) throws {
        let packages = try FileManager.default.contentsOfDirectory(at: inputs, includingPropertiesForKeys: nil).filter { $0.pathExtension == "apk" }.sorted { $0.lastPathComponent < $1.lastPathComponent }
        guard !packages.isEmpty else { throw TramesError.message("Missing signed Vulkan loader packages") }
        for file in packages { try session.transfer(guest: "/tmp/" + file.lastPathComponent, host: file, export: false) }
        let shaders = program == "vulkan-compute" ? ["vulkan-compute.spv"] : ["vulkan-render.vert.spv", "vulkan-render.frag.spv"]
        for name in [program] + shaders { try session.transfer(guest: "/tmp/" + name, host: inputs.appendingPathComponent(name), export: false) }
        let names = packages.map { "/tmp/" + $0.lastPathComponent }.joined(separator: " ")
        let command = "apk --repositories-file /dev/null --no-progress add \(names) && chmod 755 /tmp/\(program) && VK_DRIVER_FILES=/usr/share/vulkan/icd.d/trames.json /tmp/\(program) \(arguments); printf '\\nVULKAN_RESULT:%d\\n' $?\n"
        let data = Data(command.utf8)
        guard try session.send(data) == data.count else { throw TramesError.message("Vulkan acceptance input was incomplete") }
    }
    private static func execute(render: Bool, display: VulkanCanvas? = nil) async throws -> (text: String, ppm: Data?) {
        guard let resources=Bundle.main.resourceURL?.appendingPathComponent("GuestVulkan"),
              let icd=Bundle.main.url(forResource:"libtrames_vulkan_icd",withExtension:"so",subdirectory:"GuestVulkan") else {
            throw TramesError.message("Missing bundled Vulkan acceptance resources")
        }
        let inputs=resources.appendingPathComponent("acceptance",isDirectory:true)
        let directory=FileManager.default.temporaryDirectory.appendingPathComponent("vulkan-\(UUID().uuidString)",isDirectory:true)
        try FileManager.default.createDirectory(at:directory,withIntermediateDirectories:true)
        defer {try? FileManager.default.removeItem(at:directory)}
        let store=directory.appendingPathComponent("store",isDirectory:true)
        try LocalSession.install(store:store,rootfs:inputs.appendingPathComponent("alpine-minirootfs.tar.gz"),total:1<<30,available:1<<30)
        let session=try LocalSession(store:store,rootfs:nil,total:1<<30,available:1<<30,network:"off",vulkanICD:icd)
        defer {session.close()}
        if let display { try session.attachDisplay(layer: Unmanaged.passUnretained(display.metal).toOpaque(), width: 1024, height: 768) }
        var text=""
        let began=Date()
        while try session.status().phase != "running" {
            let status=try session.status()
            if status.phase != "starting" && status.phase != "running" || Date().timeIntervalSince(began)>30 {throw TramesError.message(status.error ?? "Vulkan session did not start")}
            try await Task.sleep(for:.milliseconds(10))
        }
        let program = display != nil ? "vulkan-present" : render ? "vulkan-render" : "vulkan-compute"
        let arguments = display != nil ? "/tmp/vulkan-render.vert.spv /tmp/vulkan-render.frag.spv" : render ? "/tmp/vulkan-render.vert.spv /tmp/vulkan-render.frag.spv /tmp/render.ppm" : "/tmp/vulkan-compute.spv"
        try prepare(session, inputs: inputs, program: program, arguments: arguments)
        while text.range(of:"\r\nVULKAN_RESULT:[0-9]+\r\n",options:.regularExpression)==nil {
            text+=String(decoding:try session.receive(),as:UTF8.self)
            let status=try session.status()
            if status.phase != "running" {throw TramesError.message(status.error ?? "Vulkan session stopped: \(status.phase)\n\(text.suffix(2048))")}
            if Date().timeIntervalSince(began)>180 {throw TramesError.message("Vulkan execution timed out: \(text.suffix(2048))")}
            try await Task.sleep(for:.milliseconds(10))
        }
        var ppm: Data?
        if render, text.contains("\r\nVULKAN_RESULT:0\r\n") {
            let file = directory.appendingPathComponent("render.ppm")
            try session.transfer(guest: "/tmp/render.ppm", host: file, export: true)
            ppm = try Data(contentsOf: file)
        }
        return (text, ppm)
    }
}
