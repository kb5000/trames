// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
import UIKit
final class LaunchTests: XCTestCase {
    override func setUpWithError() throws { continueAfterFailure = false }
    // SwiftTerm's indefinite cursor animation makes XCTest wait 60 seconds
    // per action. Use the existing DEBUG steady-cursor mode for every launch;
    // window gestures and UIKit animations remain enabled.
    private func launchApp(_ arguments: [String] = []) -> XCUIApplication {
        let app = XCUIApplication()
        app.launchArguments = ["--ui-acceptance"] + arguments
        app.launch()
        return app
    }
    func testWorkspaceTabsDetachDockAndTerminalTitleRemainsVisible() {
        let app=launchApp(["--workspace-ui-acceptance"])
        func element(_ prefix:String)->XCUIElement {
            app.descendants(matching:.any).matching(NSPredicate(format:"identifier BEGINSWITH %@",prefix)).firstMatch
        }
        let alpine=element("tab-terminal-"),title=element("window-title-wayland-")
        XCTAssertTrue(alpine.waitForExistence(timeout:20),app.debugDescription)
        XCTAssertTrue(title.waitForExistence(timeout:10),app.debugDescription)
        title.coordinate(withNormalizedOffset:CGVector(dx:0.25,dy:0.5)).press(forDuration:0.15,thenDragTo:alpine.coordinate(withNormalizedOffset:CGVector(dx:0.5,dy:0.5)))
        let graphic=element("tab-wayland-")
        XCTAssertTrue(graphic.waitForExistence(timeout:5),app.debugDescription)
        XCTAssertEqual(graphic.frame.midY,alpine.frame.midY,accuracy:5)
        alpine.tap();XCTAssertTrue(alpine.label.contains("Alpine"))
        graphic.coordinate(withNormalizedOffset:CGVector(dx:0.5,dy:0.5)).press(forDuration:0.15,thenDragTo:app.coordinate(withNormalizedOffset:CGVector(dx:0.55,dy:0.5)))
        XCTAssertTrue(title.waitForExistence(timeout:5),app.debugDescription)
        let dock=element("dock-wayland-");dock.tap();XCTAssertTrue(graphic.waitForExistence(timeout:5))
        alpine.tap();XCTAssertTrue(alpine.label.contains("Alpine"))
        alpine.coordinate(withNormalizedOffset:CGVector(dx:0.5,dy:0.5)).press(forDuration:0.15,thenDragTo:app.coordinate(withNormalizedOffset:CGVector(dx:0.3,dy:0.6)))
        let terminalTitle=element("window-title-terminal-")
        XCTAssertTrue(terminalTitle.waitForExistence(timeout:5),app.debugDescription)
        element("dock-terminal-").tap();XCTAssertTrue(alpine.waitForExistence(timeout:5))
        XCTAssertTrue(alpine.label.contains("Alpine"));XCTAssertEqual(graphic.frame.midY,alpine.frame.midY,accuracy:5)
    }
    func testNativeCoreMarkCanBeOpenedWithoutStartingATimedRun() {
        let app=launchApp()
        XCTAssertTrue(app.buttons["新建或管理会话"].waitForExistence(timeout: 20))
        app.buttons["新建或管理会话"].tap()
        app.buttons["原生 CoreMark"].tap()
        XCTAssertTrue(app.buttons["native-coremark-run"].waitForExistence(timeout: 10))
        XCTAssertEqual(app.staticTexts["native-coremark-status"].label, "尚未运行")
        XCTAssertTrue(app.buttons["native-coremark-run"].isEnabled)
        app.buttons["完成"].tap()
        XCTAssertTrue(app.buttons["新建或管理会话"].isHittable)
    }
    func testGuestNativeRequestRequiresVisibleConsent() {
        let app=launchApp()
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
        let app=launchApp()
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
