// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
@testable import Trames

final class LocalWaylandTests: XCTestCase {
    func testOriginalGuestClientsNativeIMEAndVkcubeReachMetalDrawables() async throws {
        let result=try await LocalWaylandProbe.run()
        let log=XCTAttachment(string:result.text);log.name="native-wayland-original-clients";log.lifetime = .keepAlways;add(log)
        let image=XCTAttachment(data:result.png,uniformTypeIdentifier:"public.png");image.name="original-vkcube-native-metal-wayland";image.lifetime = .keepAlways;add(image)
        for stage in ["info","windows","shm","editor","cube","vkmark"] { XCTAssertTrue(result.text.contains("\r\nWAYLAND_RESULT:\(stage):0\r\n"),result.text) }
        XCTAssertTrue(result.resized);XCTAssertTrue(result.textConfirmed)
        XCTAssertEqual(result.copiedBytes,0,"SHM import unexpectedly copied pixels")
        XCTAssertFalse(result.text.contains("llvmpipe"),result.text)
    }
}
