// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
@testable import Trames

final class LocalVulkanTests: XCTestCase {
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
