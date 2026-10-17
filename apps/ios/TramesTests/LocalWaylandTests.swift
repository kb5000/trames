// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
@testable import Trames

final class LocalWaylandTests: XCTestCase {
    func testOriginalGuestClientsNativeIMEAndVkcubeReachMetalDrawables() async throws {
        let result:WaylandProbeResult
        do { result=try await LocalWaylandProbe.run() }
        catch { XCTFail("Original Wayland acceptance failed: \(String(reflecting:error))");return }
        let log=XCTAttachment(string:result.text);log.name="native-wayland-original-clients";log.lifetime = .keepAlways;add(log)
        let image=XCTAttachment(data:result.png,uniformTypeIdentifier:"public.png");image.name="original-vkcube-native-metal-wayland";image.lifetime = .keepAlways;add(image)
        for stage in ["info","windows","shm","editor","cube","vkmark"] { XCTAssertTrue(result.text.contains("\r\nWAYLAND_RESULT:\(stage):0\r\n"),result.text) }
        XCTAssertTrue(result.resized);XCTAssertTrue(result.textConfirmed)
        XCTAssertTrue(result.resumedPresentation,"Vkcube did not exercise hide/restore during actual presentation")
        XCTAssertGreaterThan(result.completed,0,"No Metal command completion was observed")
        if result.presentationFeedbackAvailable {
            XCTAssertGreaterThan(result.presented,0,"No actual Metal drawable presentation was observed")
        } else {
            XCTAssertEqual(result.presented,0,"Simulator completion must not be reported as on-screen presentation")
        }
        XCTAssertEqual(result.copiedBytes,0,"SHM import unexpectedly copied pixels")
        XCTAssertFalse(result.text.contains("llvmpipe"),result.text)
    }
}
