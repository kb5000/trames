// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
import XCTest
@testable import TramesKit

final class LocalTimezoneTests: XCTestCase {
    func testSystemRulesArePreservedForNamedAndDaylightSavingZones() throws {
        for identifier in ["Asia/Shanghai", "America/New_York", "Etc/UTC"] {
            let zone = try XCTUnwrap(TimeZone(identifier: identifier))
            let captured = try LocalTimezone(zone)
            XCTAssertEqual(captured.identifier, identifier)
            XCTAssertEqual(captured.data, (zone as NSTimeZone).data)
            XCTAssertTrue(captured.data.starts(with: Data("TZif".utf8)))
            let encoded = try JSONSerialization.data(withJSONObject: captured.options)
            XCTAssertLessThanOrEqual(encoded.count, 65536)
            let decoded = try XCTUnwrap(JSONSerialization.jsonObject(with: encoded) as? [String: Any])
            XCTAssertEqual(decoded["identifier"] as? String, identifier)
            XCTAssertEqual(decoded["data"] as? [UInt8], Array(captured.data))
        }
    }
}
