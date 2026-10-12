// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
@testable import Trames
final class LocalNetworkTests: XCTestCase {
    func testRealAlpineGatewayDNSAndHTTPSOnAppleRuntime() async throws {
        let text=try await LocalNetworkProbe.run()
        XCTAssertTrue(text.contains("\r\nLOG_RESULT:0\r\n"),text)
        XCTAssertTrue(text.contains("nameserver 10.0.0.1"),text)
        XCTAssertTrue(text.contains("Name:\tdl-cdn.alpinelinux.org"),text)
        XCTAssertFalse(text.contains("DNS lookup error"),text)
        XCTAssertFalse(text.contains("Can't find"),text)
        XCTAssertTrue(text.contains("distinct packages available"),text)
        XCTAssertTrue(text.contains("\r\nNETWORK_RESULT:0\r\n"),text)
    }
}
