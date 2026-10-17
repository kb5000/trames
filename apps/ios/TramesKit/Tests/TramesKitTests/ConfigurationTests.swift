// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
@testable import TramesKit
final class ConfigurationTests: XCTestCase {
    func testLocalMemoryBudgetRejectsInvalidConfigurationBeforeOpeningStore() {
        let store = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        for (total, process) in [(0, 0), (1024, 2048), (-1, 512), ((1 << 28) + 1, 512)] {
            XCTAssertThrowsError(try LocalSession(store: store, rootfs: nil, total: 1 << 30, available: 1 << 30,
                                                 memoryBudgetMiB: total, processMemoryBudgetMiB: process))
            XCTAssertFalse(FileManager.default.fileExists(atPath: store.path))
        }
    }
}
