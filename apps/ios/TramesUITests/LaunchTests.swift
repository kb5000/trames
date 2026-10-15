// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
final class LaunchTests: XCTestCase {
    override func setUpWithError() throws { continueAfterFailure = false }
    func testGPUFrameIsActuallyVisibleInRenderSheet() {
        let app = XCUIApplication(); app.launch()
        XCTAssertTrue(app.buttons["新建或管理会话"].waitForExistence(timeout: 20))
        app.buttons["新建或管理会话"].tap()
        app.buttons["GPU 渲染示例"].tap()
        XCTAssertTrue(app.images["gpu-render-image"].waitForExistence(timeout: 180),
            app.staticTexts["gpu-render-error"].exists ? app.staticTexts["gpu-render-error"].label : app.debugDescription)
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = "visible-vulkan-triangle"
        attachment.lifetime = .keepAlways
        add(attachment)
        app.buttons["完成"].tap()
        XCTAssertTrue(app.staticTexts["TRAMES"].exists)
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
