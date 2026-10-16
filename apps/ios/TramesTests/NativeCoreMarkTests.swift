// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
@testable import Trames

final class NativeCoreMarkTests: XCTestCase {
    func testOriginalPerformanceAndValidationAlgorithmCRCs() {
        // A one-iteration smoke check must NOT be accepted as a timed score.
        // Full >=10s measurements are manually initiated on the physical iPad.
        for validation in [false, true] {
            let (code, text) = NativeCoreMarkRunner.execute(iterations: 1, validation: validation)
            XCTAssertEqual(code, 0)
            XCTAssertTrue(text.contains("Must execute for at least 10 secs"), text)
            XCTAssertFalse(text.contains("Correct operation validated"), text)
            XCTAssertFalse(text.contains("ERROR! list crc"), text)
            XCTAssertFalse(text.contains("ERROR! matrix crc"), text)
            XCTAssertFalse(text.contains("ERROR! state crc"), text)
            XCTAssertTrue(text.contains(validation ? "0x18f2" : "0xe9f5"), text)
            XCTAssertTrue(text.contains("-O2 -fno-lto"), text)
        }
    }
}
