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
        // Reaching the top edge never docks a floating window.
        XCTAssertTrue(title.exists,app.debugDescription)
        XCTAssertFalse(element("tab-wayland-").exists)
        let firstDock=element("dock-wayland-")
        XCTAssertEqual(firstDock.frame.width,44,accuracy:1);XCTAssertEqual(firstDock.frame.height,44,accuracy:1)
        firstDock.coordinate(withNormalizedOffset:CGVector(dx:0.1,dy:0.1)).tap()
        let graphic=element("tab-wayland-")
        XCTAssertTrue(graphic.waitForExistence(timeout:5),app.debugDescription)
        XCTAssertEqual(graphic.frame.midY,alpine.frame.midY,accuracy:5)
        alpine.tap();XCTAssertTrue(alpine.label.contains("Alpine"))
        graphic.coordinate(withNormalizedOffset:CGVector(dx:0.5,dy:0.5)).press(forDuration:0.15,thenDragTo:app.coordinate(withNormalizedOffset:CGVector(dx:0.55,dy:0.5)))
        XCTAssertTrue(title.waitForExistence(timeout:5),app.debugDescription)
        let resize=element("window-resize-wayland-")
        XCTAssertTrue(resize.waitForExistence(timeout:5))
        let oldRight=resize.frame.maxX,oldBottom=resize.frame.maxY
        let corner=resize.coordinate(withNormalizedOffset:CGVector(dx:0.85,dy:0.85))
        corner.press(forDuration:0.15,thenDragTo:corner.withOffset(CGVector(dx:-70,dy:-60)))
        XCTAssertLessThan(resize.frame.maxX,oldRight-40)
        XCTAssertLessThan(resize.frame.maxY,oldBottom-30)
        let minimize=element("minimize-wayland-")
        XCTAssertEqual(minimize.frame.width,44,accuracy:1);XCTAssertEqual(minimize.frame.height,44,accuracy:1)
        minimize.coordinate(withNormalizedOffset:CGVector(dx:0.1,dy:0.1)).tap()
        XCTAssertTrue(graphic.waitForExistence(timeout:5),app.debugDescription)
        XCTAssertTrue(alpine.isHittable)
        XCTAssertEqual(graphic.frame.midY,alpine.frame.midY,accuracy:5)
        graphic.tap()
        XCTAssertFalse(element("window-title-wayland-").exists)
        XCTAssertFalse(element("minimize-wayland-").exists)
        graphic.coordinate(withNormalizedOffset:CGVector(dx:0.5,dy:0.5)).press(forDuration:0.15,thenDragTo:app.coordinate(withNormalizedOffset:CGVector(dx:0.55,dy:0.5)))
        XCTAssertTrue(title.waitForExistence(timeout:5))
        let dock=element("dock-wayland-");dock.tap();XCTAssertTrue(graphic.waitForExistence(timeout:5))
        alpine.tap();XCTAssertTrue(alpine.label.contains("Alpine"))
        alpine.coordinate(withNormalizedOffset:CGVector(dx:0.5,dy:0.5)).press(forDuration:0.15,thenDragTo:app.coordinate(withNormalizedOffset:CGVector(dx:0.3,dy:0.6)))
        let terminalTitle=element("window-title-terminal-")
        XCTAssertTrue(terminalTitle.waitForExistence(timeout:5),app.debugDescription)
        element("dock-terminal-").tap();XCTAssertTrue(alpine.waitForExistence(timeout:5))
        XCTAssertTrue(alpine.label.contains("Alpine"));XCTAssertEqual(graphic.frame.midY,alpine.frame.midY,accuracy:5)
        let close=element("close-tab-terminal-")
        XCTAssertEqual(close.frame.width,44,accuracy:1);XCTAssertEqual(close.frame.height,44,accuracy:1)
        XCTAssertEqual(close.frame.minX,alpine.frame.maxX,accuracy:2)
        XCTAssertEqual(close.frame.midY,alpine.frame.midY,accuracy:2)
    }
    func testOnlyAlpineCanMinimizeRestoreAndUseTheInvisibleResizeCorner() {
        let app=launchApp()
        func element(_ prefix:String)->XCUIElement {
            app.descendants(matching:.any).matching(NSPredicate(format:"identifier BEGINSWITH %@",prefix)).firstMatch
        }
        let state=app.staticTexts["session-state"]
        let ready=XCTNSPredicateExpectation(predicate:NSPredicate(format:"label == %@","Alpine · 本地"),object:state)
        XCTAssertEqual(XCTWaiter.wait(for:[ready],timeout:120),.completed)
        XCTAssertTrue(app.staticTexts["cpu-usage"].waitForExistence(timeout:6))
        for iteration in 0..<3 {
            let tab=element("tab-terminal-")
            tab.coordinate(withNormalizedOffset:CGVector(dx:0.5,dy:0.5)).press(forDuration:0.15,
                thenDragTo:app.coordinate(withNormalizedOffset:CGVector(dx:0.3,dy:0.6)))
            let title=element("window-title-terminal-")
            XCTAssertTrue(title.waitForExistence(timeout:5),app.debugDescription)
            if iteration==0 {
                let corner=element("window-resize-terminal-")
                XCTAssertTrue(corner.waitForExistence(timeout:5))
                XCTAssertFalse(app.images.matching(NSPredicate(format:"identifier BEGINSWITH %@","window-resize-")).firstMatch.exists)
                let oldRight=corner.frame.maxX
                let point=corner.coordinate(withNormalizedOffset:CGVector(dx:0.85,dy:0.85))
                point.press(forDuration:0.15,thenDragTo:point.withOffset(CGVector(dx:-60,dy:-40)))
                XCTAssertLessThan(corner.frame.maxX,oldRight-30)
            }
            element("minimize-terminal-").tap()
            XCTAssertTrue(tab.waitForExistence(timeout:5),app.debugDescription)
            let hidden=XCTNSPredicateExpectation(predicate:NSPredicate(format:"exists == false"),object:title)
            XCTAssertEqual(XCTWaiter.wait(for:[hidden],timeout:5),.completed)
            app.buttons["新建或管理会话"].tap();app.buttons["设置"].tap()
            XCTAssertTrue(app.buttons["完成"].waitForExistence(timeout:5));app.buttons["完成"].tap()
            tab.tap()
            XCTAssertTrue(state.waitForExistence(timeout:5),app.debugDescription)
        }
        app.descendants(matching:.any).matching(identifier:"terminal-viewport").firstMatch.tap()
        app.typeText("exit\n")
        let exited=XCTNSPredicateExpectation(predicate:NSPredicate(format:"label BEGINSWITH %@","已退出"),object:state)
        XCTAssertEqual(XCTWaiter.wait(for:[exited],timeout:30),.completed)
    }
    func testSessionMenuContainsOnlyProductFeatures() {
        let app=launchApp(["--workspace-ui-acceptance"])
        XCTAssertTrue(app.buttons["新建或管理会话"].waitForExistence(timeout:20))
        app.buttons["新建或管理会话"].tap()
        XCTAssertTrue(app.buttons["本地 Alpine"].waitForExistence(timeout:5))
        XCTAssertTrue(app.buttons["Files"].exists);XCTAssertTrue(app.buttons["设置"].exists)
        XCTAssertFalse(app.buttons["原生 CoreMark"].exists)
        XCTAssertFalse(app.buttons["SSH 连接"].exists);XCTAssertFalse(app.buttons["Files / SFTP"].exists)
    }
    func testGuestNativeRequestRequiresVisibleConsent() {
        let app=launchApp()
        let ready=XCTNSPredicateExpectation(predicate:NSPredicate(format:"label == %@","Alpine · 本地"),object:app.staticTexts["session-state"])
        XCTAssertEqual(XCTWaiter.wait(for:[ready],timeout:120),.completed)
        // Exercise the actual running VM/PTY, rather than only the no-VM
        // workspace gesture fixture, then execute a command through that PTY.
        let terminalTab=app.descendants(matching:.any).matching(NSPredicate(format:"identifier BEGINSWITH %@","tab-terminal-")).firstMatch
        for _ in 0..<2 {
            terminalTab.coordinate(withNormalizedOffset:CGVector(dx:0.5,dy:0.5)).press(forDuration:0.15,
                thenDragTo:app.coordinate(withNormalizedOffset:CGVector(dx:0.3,dy:0.6)))
            let dock=app.buttons.matching(NSPredicate(format:"identifier BEGINSWITH %@","dock-terminal-")).firstMatch
            XCTAssertTrue(dock.waitForExistence(timeout:5),app.debugDescription)
            dock.tap()
            XCTAssertTrue(terminalTab.waitForExistence(timeout:5))
        }
        app.descendants(matching:.any).matching(identifier:"terminal-viewport").firstMatch.tap()
        app.typeText("trames bridge '{\"version\":1,\"id\":1,\"method\":\"device.info\",\"params\":{}}'\n")
        XCTAssertTrue(app.buttons["bridge-deny"].waitForExistence(timeout:20))
        app.buttons["bridge-deny"].tap()
        let gone=XCTNSPredicateExpectation(predicate:NSPredicate(format:"exists == false"),object:app.buttons["bridge-deny"])
        XCTAssertEqual(XCTWaiter.wait(for:[gone],timeout:10),.completed)
        app.descendants(matching:.any).matching(identifier:"terminal-viewport").firstMatch.tap()
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
        XCTAssertTrue(app.staticTexts["cpu-usage"].waitForExistence(timeout:5))
        XCUIDevice.shared.orientation = .landscapeLeft
        let terminal=app.descendants(matching:.any).matching(identifier:"terminal-viewport").firstMatch
        XCTAssertTrue(terminal.waitForExistence(timeout:10));terminal.tap()
        XCTAssertFalse(app.buttons["Ctrl-C"].exists,"The duplicate App shortcut row must be absent")
        app.typeText("exit\n")
        let exited = XCTNSPredicateExpectation(predicate: NSPredicate(format: "label BEGINSWITH %@", "已退出"), object: state)
        XCTAssertEqual(XCTWaiter.wait(for: [exited], timeout: 30), .completed)
        app.buttons.matching(NSPredicate(format:"identifier BEGINSWITH %@","close-tab-terminal-")).firstMatch.tap()
        app.buttons["新建或管理会话"].tap()
        app.buttons["本地 Alpine"].tap()
        let reopened = XCTNSPredicateExpectation(predicate: NSPredicate(format: "label == %@", "Alpine · 本地"), object: app.staticTexts["session-state"])
        XCTAssertEqual(XCTWaiter.wait(for: [reopened], timeout: 30), .completed)
        XCUIDevice.shared.orientation = .portrait
    }
}
