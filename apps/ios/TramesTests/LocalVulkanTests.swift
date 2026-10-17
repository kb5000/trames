// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
@testable import Trames

final class LocalVulkanTests: XCTestCase {
    func testCoherentMappingsPreserveCpuWritesAndGpuResultsWithoutExplicitTransfers() async throws {
        let text = try await LocalVulkanProbe.coherent()
        let log = XCTAttachment(string: text)
        log.name = "native-vulkan-coherent-log"; log.lifetime = .keepAlways; add(log)
        XCTAssertTrue(text.contains("coherent unmap/remap preserves CPU writes: ok"), text)
        XCTAssertTrue(text.contains("coherent persistent mappings without flush/invalidate preserve independent CPU input and GPU output ranges: ok"), text)
        XCTAssertTrue(text.contains("compute round 3: 4096 uint32 values correct"), text)
        XCTAssertTrue(text.contains("vulkan-compute-ok"), text)
        XCTAssertTrue(text.contains("\r\nVULKAN_RESULT:0\r\n"), text)
        XCTAssertFalse(text.contains("llvmpipe"), text)
    }
    func testStandardDisplaySwapchainRecreationAndProducerRelease() async throws {
        let text = try await LocalVulkanProbe.present()
        let log = XCTAttachment(string: text)
        log.name = "native-vulkan-display-log"; log.lifetime = .keepAlways; add(log)
        XCTAssertTrue(text.contains("present round 2: 1024x768 native swapchain active"), text)
        XCTAssertTrue(text.contains("vulkan-present-ok frames=24"), text)
        XCTAssertTrue(text.contains("\r\nVULKAN_RESULT:0\r\n"), text)
        XCTAssertFalse(text.contains("llvmpipe"), text)
    }
    @MainActor
    func testWindowResizePreservesMetalPixelExtent() {
        let canvas = VulkanCanvas()
        for size in [CGSize(width: 600, height: 400), CGSize(width: 240, height: 190), CGSize(width: 1024, height: 768)] {
            canvas.frame = CGRect(origin: .zero, size: size)
            canvas.layoutIfNeeded()
            XCTAssertEqual(canvas.metal.drawableSize, CGSize(width: 1024, height: 768))
            XCTAssertEqual(canvas.metal.bounds.size, CGSize(width: 1024, height: 768))
        }
        var geometry = DisplayWindowGeometry(origin: CGPoint(x: 900, y: -50), size: CGSize(width: 2000, height: 10))
        geometry.constrain(to: CGSize(width: 800, height: 600))
        XCTAssertEqual(geometry.size, CGSize(width: 800, height: 200))
        XCTAssertEqual(geometry.origin, .zero)
    }
    func testStandardGuestGraphicsRendersNativeMetalPixels() async throws {
        let result = try await LocalVulkanProbe.render()
        XCTAssertTrue(result.text.contains("render round 3: barycentric colors, alpha, background and scissor correct"), result.text)
        XCTAssertTrue(result.text.contains("Vulkan render driver:"), result.text)
        XCTAssertFalse(result.text.contains("llvmpipe"), result.text)
        XCTAssertEqual(result.image.cgImage?.width, 256)
        XCTAssertEqual(result.image.cgImage?.height, 192)
        let image = try XCTUnwrap(result.image.pngData())
        let attachment = XCTAttachment(data: image, uniformTypeIdentifier: "public.png")
        attachment.name = "native-metal-vulkan-triangle"
        attachment.lifetime = .keepAlways
        add(attachment)
    }
    func testIndexedTexturesUseNativeMetalSamplersAndGraphicsDescriptors() async throws {
        let result = try await LocalVulkanProbe.texture()
        for expected in ["uint16 indexed nearest", "uint32 indexed linear", "non-indexed nearest", "vulkan-texture-ok"] {
            XCTAssertTrue(result.text.contains(expected), result.text)
        }
        XCTAssertFalse(result.text.contains("llvmpipe"), result.text)
        let log = XCTAttachment(string: result.text)
        log.name = "native-vulkan-texture-log"; log.lifetime = .keepAlways; add(log)
        let image = try XCTUnwrap(result.image.pngData())
        let attachment = XCTAttachment(data: image, uniformTypeIdentifier: "public.png")
        attachment.name = "native-metal-vulkan-texture"; attachment.lifetime = .keepAlways; add(attachment)
    }
    func testNativeAnisotropyTransferClearAndStaticAlphaBlendPixels() async throws {
        for (mode, marker) in [("anisotropy", "native anisotropic sampler and transfer color clear: correct"),
                               ("static-blend", "static viewport/scissor and source-alpha blending: correct")] {
            let result = try await LocalVulkanProbe.texture(mode: mode)
            XCTAssertTrue(result.text.contains(marker), result.text)
            XCTAssertFalse(result.text.contains("llvmpipe"), result.text)
            let log = XCTAttachment(string: result.text); log.name = "native-vulkan-" + mode; log.lifetime = .keepAlways; add(log)
        }
    }
    func testIncompleteOrUnexpectedImageCannotBeDisplayed() {
        for ppm in [Data(), Data("P6\n256 192\n255\n".utf8), Data(repeating: 0, count: 256 * 192 * 3 + 15)] {
            XCTAssertThrowsError(try VulkanRenderResult(text: "", ppm: ppm))
        }
    }
    func testStandardGuestVulkanComputeUsesNativeMetal() async throws {
        let text=try await LocalVulkanProbe.run()
        XCTAssertTrue(text.contains("vulkan-compute-ok"),text)
        XCTAssertTrue(text.contains("pipeline compile with active peer: ok"),text)
        XCTAssertTrue(text.contains("parallel pipeline callers preserve default/specialized outputs: ok"),text)
        XCTAssertTrue(text.contains("compute round 3: 4096 uint32 values correct"),text)
        XCTAssertTrue(text.contains("compute semaphore chain 2: 4096 specialized uint32 values correct"),text)
        XCTAssertTrue(text.contains("binary semaphore reuse and empty submissions: ok"),text)
        XCTAssertTrue(text.contains("partial mapping, offset flush/invalidate and remap persistence: ok"),text)
        XCTAssertTrue(text.contains("unaligned map offset preserves Vulkan pointer alignment: ok"),text)
        XCTAssertTrue(text.contains("Vulkan driver:"),text)
        XCTAssertFalse(text.contains("llvmpipe"),text)
        XCTAssertTrue(text.contains("\r\nVULKAN_RESULT:0\r\n"),text)
    }
}
