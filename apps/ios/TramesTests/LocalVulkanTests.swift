// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
@testable import Trames

final class LocalVulkanTests: XCTestCase {
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
