// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
final class LaunchTests: XCTestCase {
    override func setUpWithError() throws { continueAfterFailure = false }
    func testNativeSwapchainPresentsWhileWindowMovesResizesAndHides() {
        let app = XCUIApplication(); app.launchArguments = ["--vulkan-display-acceptance"]; app.launch()
        let canvas = app.otherElements["vulkan-display-canvas"]
        XCTAssertTrue(canvas.waitForExistence(timeout: 120), app.debugDescription)
        let active = XCTNSPredicateExpectation(predicate: NSPredicate(format: "value MATCHES %@", "1024x768 frames=[1-9][0-9]+"), object: canvas)
        XCTAssertEqual(XCTWaiter.wait(for: [active], timeout: 180), .completed, app.debugDescription)
        let frameCount = canvas.value as? String ?? ""
        let title = app.descendants(matching: .any).matching(identifier: "vulkan-display-title").firstMatch
        let before = canvas.frame
        title.coordinate(withNormalizedOffset: CGVector(dx: 0.3, dy: 0.5)).press(forDuration: 0.1, thenDragTo: title.coordinate(withNormalizedOffset: CGVector(dx: 0.3, dy: 0.5)).withOffset(CGVector(dx: 45, dy: 45)))
        XCTAssertGreaterThan(canvas.frame.minX, before.minX + 20)
        let resize = app.images["vulkan-display-resize"]
        resize.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.5)).press(forDuration: 0.1, thenDragTo: resize.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.5)).withOffset(CGVector(dx: -90, dy: -70)))
        XCTAssertLessThan(canvas.frame.width, before.width - 40)
        let continued = XCTNSPredicateExpectation(predicate: NSPredicate(format: "value != %@ AND value BEGINSWITH %@", frameCount, "1024x768 frames="), object: canvas)
        XCTAssertEqual(XCTWaiter.wait(for: [continued], timeout: 20), .completed)
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = "native-vulkan-swapchain-window"
        attachment.lifetime = .keepAlways; add(attachment)
        app.buttons["隐藏显示窗口"].tap()
        XCTAssertTrue(app.buttons["新建或管理会话"].isHittable)
        app.buttons["新建或管理会话"].tap(); app.buttons["显示窗口"].tap()
        XCTAssertTrue(canvas.waitForExistence(timeout: 10))
        // Closing a continuously submitting guest exercises teardown with GPU work.
        app.buttons["关闭当前终端"].tap()
        XCTAssertTrue(app.staticTexts["打开一个终端"].waitForExistence(timeout: 20))
    }
    func testGuestNativeRequestRequiresVisibleConsent() {
        let app=XCUIApplication(); app.launchArguments=["--ui-acceptance"]; app.launch()
        let ready=XCTNSPredicateExpectation(predicate:NSPredicate(format:"label == %@","Alpine · 本地"),object:app.staticTexts["session-state"])
        XCTAssertEqual(XCTWaiter.wait(for:[ready],timeout:120),.completed)
        app.buttons["terminal-keyboard"].tap()
        app.typeText("trames bridge '{\"version\":1,\"id\":1,\"method\":\"device.info\",\"params\":{}}'\n")
        XCTAssertTrue(app.buttons["bridge-deny"].waitForExistence(timeout:20))
        app.buttons["bridge-deny"].tap()
        let gone=XCTNSPredicateExpectation(predicate:NSPredicate(format:"exists == false"),object:app.buttons["bridge-deny"])
        XCTAssertEqual(XCTWaiter.wait(for:[gone],timeout:10),.completed)
        app.buttons["terminal-keyboard"].tap()
        app.typeText("trames bridge '{\"version\":1,\"id\":2,\"method\":\"device.info\",\"params\":{}}'\n")
        XCTAssertTrue(app.buttons["bridge-allow"].waitForExistence(timeout:20))
        app.buttons["bridge-allow"].tap()
        let accepted=XCTNSPredicateExpectation(predicate:NSPredicate(format:"exists == false"),object:app.buttons["bridge-allow"])
        XCTAssertEqual(XCTWaiter.wait(for:[accepted],timeout:10),.completed)
    }
    func testLaunchAlpineRotateExitAndReopenPersistentStore() {
        let app = XCUIApplication()
        app.launch()
        XCTAssertTrue(app.staticTexts["TRAMES"].waitForExistence(timeout: 20))
        let state = app.staticTexts["session-state"]
        let ready = XCTNSPredicateExpectation(predicate: NSPredicate(format: "label == %@", "Alpine · 本地"), object: state)
        XCTAssertEqual(XCTWaiter.wait(for: [ready], timeout: 120), .completed,
            "Alpine state: \(state.exists ? state.label : "missing")")
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
