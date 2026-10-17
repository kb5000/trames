// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
import UIKit
import SwiftTerm
@testable import Trames

@MainActor
final class TerminalViewportTests: XCTestCase {
    private func makeViewport() -> TerminalViewport {
        let view = TerminalViewport(frame: CGRect(x: 0, y: 0, width: 320, height: 160),
                                    font: .monospacedSystemFont(ofSize: 14, weight: .regular))
        view.feed(text: (0..<100).map { "left-edge-\($0)\r\n" }.joined())
        view.setNeedsLayout()
        view.layoutIfNeeded()
        return view
    }

    func testAccessoryControllerReservesSystemCornerAndPreservesCtrl() throws {
        let view=makeViewport();defer {view.updateUiClosed()}
        let original=try XCTUnwrap(view.inputAccessoryView as? TerminalAccessory)
        original.controlModifier=true
        let controller=try XCTUnwrap(view.inputAccessoryViewController as? TerminalKeyboardAccessoryController)
        controller.loadViewIfNeeded()
        for width:CGFloat in [375,768,1024] {
            controller.view.frame=CGRect(x:0,y:0,width:width,height:48)
            controller.view.setNeedsLayout();controller.view.layoutIfNeeded()
            XCTAssertTrue(controller.view === original)
            XCTAssertTrue(view.inputAccessoryView === original)
            XCTAssertTrue(original.controlModifier)
            let toggle=try XCTUnwrap(original.subviews.first {$0.accessibilityIdentifier=="terminal-extended-keyboard-toggle"})
            let frame=toggle.convert(toggle.bounds,to:controller.view)
            XCTAssertLessThanOrEqual(frame.maxX,width-TerminalKeyboardAccessoryController.trailingClearance)
            XCTAssertGreaterThan(frame.width,0)
        }
    }

    func testGestureAndProgrammaticOffsetsKeepTheLeftEdgeAndVerticalScroll() {
        let view = makeViewport()
        defer { view.updateUiClosed() }
        XCTAssertGreaterThan(view.contentSize.height, view.bounds.height)
        for x: CGFloat in [-120, 120] {
            view.contentOffset = CGPoint(x: x, y: 100)
            XCTAssertEqual(view.contentOffset.x, 0)
            XCTAssertEqual(view.bounds.minX, 0)
            XCTAssertEqual(view.contentOffset.y, 100)
            view.setContentOffset(CGPoint(x: x, y: 200), animated: false)
            XCTAssertEqual(view.contentOffset.x, 0)
            XCTAssertEqual(view.contentOffset.y, 200)
            view.bounds.origin = CGPoint(x: x, y: 150)
            XCTAssertEqual(view.bounds.minX, 0)
            XCTAssertEqual(view.bounds.minY, 150)
        }
    }

    func testRotationKeyboardAndFractionalResizesKeepColumnsInsideViewport() {
        let view = makeViewport()
        defer { view.updateUiClosed() }
        let portraitColumns = view.getTerminal().cols
        for size in [CGSize(width: 640, height: 160), CGSize(width: 320, height: 80),
                     CGSize(width: 319.75, height: 80), CGSize(width: 320, height: 160)] {
            view.frame.size = size
            view.setNeedsLayout()
            view.layoutIfNeeded()
            XCTAssertEqual(view.contentSize.width, size.width)
            XCTAssertLessThanOrEqual(view.getOptimalFrameSize().width, size.width)
            XCTAssertEqual(view.bounds.minX, 0)
            if size.width == 640 { XCTAssertGreaterThan(view.getTerminal().cols, portraitColumns) }
        }
        XCTAssertEqual(view.getTerminal().cols, portraitColumns)
    }

    func testWideOutputAndContentSizeChangesCannotEnableHorizontalScrolling() {
        let view = makeViewport()
        defer { view.updateUiClosed() }
        view.feed(text: String(repeating: "wide-output ", count: 100) + "\r\n")
        let scrollbackHeight = view.contentSize.height
        view.contentSize = CGSize(width: 2000, height: scrollbackHeight)
        XCTAssertEqual(view.contentSize.width, view.bounds.width)
        XCTAssertEqual(view.contentSize.height, scrollbackHeight)
        XCTAssertFalse(view.alwaysBounceHorizontal)
        XCTAssertFalse(view.showsHorizontalScrollIndicator)
        XCTAssertEqual(view.contentInsetAdjustmentBehavior, .never)
    }

    func testProgressRedrawAtRightmostColumnPreservesOldAndNewLeftEdges() {
        let view = makeViewport()
        defer { view.updateUiClosed() }
        view.feed(text: "\u{1b}[2J\u{1b}[3J\u{1b}[H")
        let columns = view.getTerminal().cols
        let rows = view.getTerminal().rows
        view.feed(text: String(repeating: "LEFT-HISTORY\r\n", count: rows - 1))
        for percent in [0, 25, 50, 75, 100] {
            // apk-tools 2.14 print.c emits ESC 7, "%3i%% ", a columns-6 bar,
            // then ESC 8 + CSI 0 K. It flushes before restoring the cursor.
            let width = columns - 6
            let filled = width * percent / 100
            let bar = String(repeating: "\u{2588}", count: filled) + String(repeating: " ", count: width - filled)
            let output = "\u{1b}7" + String(format: "%3d%% ", percent) + bar
            // Replay one byte at a time to cover split escape sequences and
            // split UTF-8 progress glyphs in separate PTY output chunks.
            for byte in output.utf8 { view.feed(byteArray: [byte][...]) }
            XCTAssertEqual(view.getTerminal().getCursorLocation().x, columns - 1)
            XCTAssertEqual(view.getTerminal().getCursorLocation().y, rows - 1)
            XCTAssertEqual(view.bounds.minX, 0)
            XCTAssertEqual(view.contentSize.width, view.bounds.width)
            XCTAssertTrue(view.getTerminal().getLine(row: 0)?.translateToString(trimRight: true).hasPrefix("LEFT-HISTORY") == true)
            for byte in "\u{1b}8\u{1b}[0K".utf8 { view.feed(byteArray: [byte][...]) }
            XCTAssertEqual(view.getTerminal().getCursorLocation().x, 0)
        }
        view.feed(text: "\r\nLEFT-NEW")
        XCTAssertTrue(view.getTerminal().getLine(row: rows - 1)?.translateToString(trimRight: true).hasPrefix("LEFT-NEW") == true)
        XCTAssertEqual(view.contentOffset.x, 0)
    }
}
