// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
import UIKit
final class LaunchTests: XCTestCase {
    override func setUpWithError() throws { continueAfterFailure = false }
    func testWorkspaceTabsDetachDockAndTerminalTitleRemainsVisible() {
        let app=XCUIApplication();app.launchArguments=["--workspace-ui-acceptance"];app.launch()
        func element(_ prefix:String)->XCUIElement {
            app.descendants(matching:.any).matching(NSPredicate(format:"identifier BEGINSWITH %@",prefix)).firstMatch
        }
        let alpine=element("tab-terminal-"),title=element("window-title-wayland-")
        XCTAssertTrue(alpine.waitForExistence(timeout:20));XCTAssertTrue(title.waitForExistence(timeout:10))
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
        let app = XCUIApplication(); app.launch()
        XCTAssertTrue(app.buttons["新建或管理会话"].waitForExistence(timeout: 20))
        app.buttons["新建或管理会话"].tap()
        app.buttons["原生 CoreMark"].tap()
        XCTAssertTrue(app.buttons["native-coremark-run"].waitForExistence(timeout: 10))
        XCTAssertEqual(app.staticTexts["native-coremark-status"].label, "尚未运行")
        XCTAssertTrue(app.buttons["native-coremark-run"].isEnabled)
        app.buttons["完成"].tap()
        XCTAssertTrue(app.buttons["新建或管理会话"].isHittable)
    }
    func testNativeSwapchainPresentsWhileWindowMovesResizesAndHides() {
        let app = XCUIApplication(); app.launchArguments = ["--vulkan-display-acceptance"]; app.launch()
        let canvas = app.otherElements["vulkan-display-canvas"].firstMatch
        XCTAssertTrue(canvas.waitForExistence(timeout: 120), app.debugDescription)
        let active = XCTNSPredicateExpectation(predicate: NSPredicate(format: "value MATCHES %@", "1024x768 frames=[1-9][0-9]+"), object: canvas)
        XCTAssertEqual(XCTWaiter.wait(for: [active], timeout: 180), .completed, app.debugDescription)
        let initial = XCTAttachment(screenshot: app.screenshot())
        initial.name = "native-vulkan-swapchain-window-initial"; initial.lifetime = .keepAlways; add(initial)
        let frameCount = canvas.value as? String ?? ""
        let title = app.descendants(matching: .any).matching(identifier: "vulkan-display-title").firstMatch
        let before = canvas.frame
        title.coordinate(withNormalizedOffset: CGVector(dx: 0.3, dy: 0.5)).press(forDuration: 0.1, thenDragTo: title.coordinate(withNormalizedOffset: CGVector(dx: 0.3, dy: 0.5)).withOffset(CGVector(dx: 45, dy: 45)))
        let moved = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in canvas.frame.minX > before.minX + 20 }, object: canvas)
        XCTAssertEqual(XCTWaiter.wait(for: [moved], timeout: 5), .completed, "Global drag must move the window by the finger displacement")
        let resize = app.descendants(matching: .any).matching(identifier: "vulkan-display-resize").firstMatch
        resize.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.5)).press(forDuration: 0.1, thenDragTo: resize.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.5)).withOffset(CGVector(dx: -90, dy: -70)))
        let resized = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in canvas.frame.width < before.width - 40 }, object: canvas)
        XCTAssertEqual(XCTWaiter.wait(for: [resized], timeout: 5), .completed, "Resize must change the window while keeping the Metal pixel extent fixed")
        let continued = XCTNSPredicateExpectation(predicate: NSPredicate(format: "value != %@ AND value BEGINSWITH %@", frameCount, "1024x768 frames="), object: canvas)
        XCTAssertEqual(XCTWaiter.wait(for: [continued], timeout: 20), .completed)
        // Verify the actual screen contains rasterized colors, rather than
        // accepting a successful present counter over a blank Metal layer.
        let image = canvas.screenshot().image.cgImage!
        var pixels = [UInt8](repeating: 0, count: 128 * 96 * 4)
        let colored = pixels.withUnsafeMutableBytes { bytes -> Int in
            let context = CGContext(data: bytes.baseAddress, width: 128, height: 96,
                bitsPerComponent: 8, bytesPerRow: 128 * 4, space: CGColorSpaceCreateDeviceRGB(),
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue)!
            context.draw(image, in: CGRect(x: 0, y: 0, width: 128, height: 96))
            let raw = bytes.bindMemory(to: UInt8.self)
            return stride(from: 0, to: raw.count, by: 4).filter { i in
                let high = max(raw[i], raw[i+1], raw[i+2]), low = min(raw[i], raw[i+1], raw[i+2])
                return high > 80 && Int(high) - Int(low) > 40
            }.count
        }
        XCTAssertGreaterThan(colored, 1000, "Native swapchain must be visible in the actual screen capture")
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = "native-vulkan-swapchain-window"
        attachment.lifetime = .keepAlways; add(attachment)
        app.buttons["隐藏显示窗口"].tap()
        XCTAssertTrue(app.buttons["新建或管理会话"].isHittable)
        app.buttons["新建或管理会话"].tap(); app.buttons["显示窗口"].tap()
        XCTAssertTrue(canvas.waitForExistence(timeout: 10))
        let reopened = UInt64((canvas.value as? String ?? "").split(separator: "=").last ?? "0") ?? 0
        let resumed = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in
            let frames = UInt64((canvas.value as? String ?? "").split(separator: "=").last ?? "0") ?? 0
            return frames >= reopened + 4
        }, object: canvas)
        XCTAssertEqual(XCTWaiter.wait(for: [resumed], timeout: 20), .completed, "Presentation must resume after revealing the existing window")
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
