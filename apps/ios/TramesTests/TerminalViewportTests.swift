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
        view.feed(text: "\u{1b}[2J\u{1b}[3J\u{1b}[HLEFT-HISTORY\r\n")
        let columns = view.getTerminal().cols
        for percent in [0, 25, 50, 75, 100] {
            // Exercise progress output: CR, erase line, colored text, a full
            // width progress bar and an absolute move to the rightmost column.
            let progress = String(repeating: "#", count: max(0, columns - 8))
            view.feed(text: "\r\u{1b}[2K\u{1b}[32m\(percent)% \(progress)\u{1b}[0m\u{1b}[\(columns)G")
            XCTAssertEqual(view.getTerminal().getCursorLocation().x, columns - 1)
            XCTAssertEqual(view.bounds.minX, 0)
            XCTAssertEqual(view.contentSize.width, view.bounds.width)
            XCTAssertTrue(view.getTerminal().getLine(row: 0)?.translateToString(trimRight: true).hasPrefix("LEFT-HISTORY") == true)
        }
        view.feed(text: "\r\nLEFT-NEW")
        XCTAssertTrue(view.getTerminal().getLine(row: 2)?.translateToString(trimRight: true).hasPrefix("LEFT-NEW") == true)
        XCTAssertEqual(view.contentOffset.x, 0)
    }
}
