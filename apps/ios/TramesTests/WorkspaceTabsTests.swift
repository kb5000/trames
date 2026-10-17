// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
import XCTest
#if canImport(Trames)
@testable import Trames
#endif

final class WorkspaceTabsTests: XCTestCase {
    func testTerminalRegainsFocusWithFloatingGraphicsStillVisible() {
        let owner=UUID(),terminal=WorkspaceItemID.terminal(UUID())
        let editor=WorkspaceItemID.wayland(owner,1)
        var state=WorkspaceTabs();state.insert(terminal,as:.tabbed);state.insert(editor,as:.floating)
        XCTAssertEqual(state.selected,terminal);XCTAssertEqual(state.focused,editor)
        state.select(terminal)
        XCTAssertEqual(state.focused,terminal);XCTAssertEqual(state.floating,[editor])
        state.focus(editor)
        XCTAssertEqual(state.selected,terminal);XCTAssertEqual(state.focused,editor)
    }
    func testDockedGraphicsArePeersOfTerminalAndCanBeReordered() {
        let owner=UUID(),terminal=WorkspaceItemID.terminal(owner),editor=WorkspaceItemID.wayland(owner,4)
        var state=WorkspaceTabs();state.insert(terminal,as:.tabbed);state.insert(editor,as:.floating)
        state.dock(editor,before:terminal)
        XCTAssertEqual(state.tabs,[editor,terminal]);XCTAssertEqual(state.selected,editor)
        XCTAssertTrue(state.floating.isEmpty)
        state.select(terminal);XCTAssertEqual(state.selected,terminal)
        state.dock(editor);XCTAssertEqual(state.tabs,[terminal,editor])
    }
    func testRepeatedDockDetachRetainsIdentityAndGeometry() {
        let id=WorkspaceItemID.terminal(UUID());var state=WorkspaceTabs();state.insert(id,as:.tabbed)
        state.frames[id]=DisplayWindowGeometry(origin:CGPoint(x:40,y:80),size:CGSize(width:500,height:400))
        for _ in 0..<5 { state.detach(id);state.dock(id);state.dock(id) }
        XCTAssertEqual(state.tabs,[id]);XCTAssertEqual(state.frames[id]?.size.width,500)
        state.detach(id,at:CGPoint(x:100,y:120))
        XCTAssertEqual(state.floating,[id]);XCTAssertEqual(state.frames[id]?.origin,CGPoint(x:100,y:120))
    }
    func testMinimizedFloatingWindowReturnsToInactiveTopTab() {
        let a=WorkspaceItemID.terminal(UUID()),b=WorkspaceItemID.terminal(UUID())
        var state=WorkspaceTabs();state.insert(a,as:.tabbed);state.insert(b,as:.floating)
        state.minimize(b)
        XCTAssertEqual(state.tabs,[a,b]);XCTAssertTrue(state.floating.isEmpty)
        XCTAssertEqual(state.minimized,[b]);XCTAssertEqual(state.selected,a);XCTAssertEqual(state.focused,a)
        state.restore(b)
        XCTAssertEqual(state.tabs,[a,b]);XCTAssertEqual(state.selected,b);XCTAssertEqual(state.focused,b)
        XCTAssertTrue(state.floating.isEmpty);XCTAssertTrue(state.minimized.isEmpty)
    }
    func testAllMinimizedTabsStayDiscoverableWithoutSelectingHiddenContent() {
        let a=WorkspaceItemID.terminal(UUID()),b=WorkspaceItemID.terminal(UUID())
        var state=WorkspaceTabs();state.insert(a,as:.tabbed);state.insert(b,as:.minimized)
        state.minimize(a)
        XCTAssertNil(state.selected);XCTAssertNil(state.focused)
        XCTAssertEqual(Set(state.tabs),Set([a,b]))
        state.select(b);XCTAssertEqual(state.selected,b);XCTAssertEqual(state.focused,b)
        state.close(b);XCTAssertNil(state.selected);XCTAssertNil(state.focused)
        XCTAssertEqual(state.tabs,[a])
        state.restore(a);XCTAssertEqual(state.selected,a)
    }
    func testClosingGraphicsDoesNotCloseOwningTerminalOrOtherSessions() {
        let owner=UUID(),a=WorkspaceItemID.terminal(owner),b=WorkspaceItemID.wayland(owner,1),c=WorkspaceItemID.terminal(UUID())
        var state=WorkspaceTabs();state.insert(a,as:.tabbed);state.insert(c,as:.tabbed);state.insert(b,as:.tabbed)
        state.close(b);XCTAssertEqual(state.tabs,[a,c]);XCTAssertEqual(state.selected,c)
        state.closeSession(owner);XCTAssertEqual(state.tabs,[c]);XCTAssertEqual(state.focused,c)
    }
    func testForeignAndStaleItemsCannotBeActivated() {
        let id=WorkspaceItemID.terminal(UUID());var state=WorkspaceTabs()
        state.dock(id);state.detach(id);state.focus(id);state.select(id)
        XCTAssertTrue(state.tabs.isEmpty);XCTAssertTrue(state.floating.isEmpty);XCTAssertNil(state.focused)
    }
}
