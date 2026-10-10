// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
final class LaunchTests: XCTestCase {
    func testLaunchAlpineRotateExitAndReopenPersistentStore() {
        let app = XCUIApplication()
        app.launch()
        XCTAssertTrue(app.staticTexts["TRAMES"].waitForExistence(timeout: 20))
        let state = app.staticTexts["session-state"]
        let ready = XCTNSPredicateExpectation(predicate: NSPredicate(format: "label == %@", "Alpine · 本地"), object: state)
        XCTAssertEqual(XCTWaiter.wait(for: [ready], timeout: 120), .completed)
        XCUIDevice.shared.orientation = .landscapeLeft
        XCTAssertTrue(app.buttons["Ctrl-D"].waitForExistence(timeout: 10))
        app.buttons["Ctrl-D"].tap()
        let exited = XCTNSPredicateExpectation(predicate: NSPredicate(format: "label BEGINSWITH %@", "已退出"), object: state)
        XCTAssertEqual(XCTWaiter.wait(for: [exited], timeout: 30), .completed)
        app.buttons["关闭当前终端"].tap()
        app.buttons["新建或管理会话"].tap()
        app.buttons["本地 Alpine"].tap()
        let reopened = XCTNSPredicateExpectation(predicate: NSPredicate(format: "label == %@", "Alpine · 本地"), object: app.staticTexts["session-state"])
        XCTAssertEqual(XCTWaiter.wait(for: [reopened], timeout: 30), .completed)
        XCUIDevice.shared.orientation = .portrait
    }
}
