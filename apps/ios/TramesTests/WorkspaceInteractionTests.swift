// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
import UIKit
import TramesKit
@testable import Trames

@MainActor
final class WorkspaceInteractionTests: XCTestCase {
    func testEmptyOscTitleFallsBackWithoutChangingLocalIdentity() {
        let local=TerminalSession(title:"Alpine",isLocal:true),ssh=TerminalSession(title:"my-server")
        defer { local.terminal.updateUiClosed();ssh.terminal.updateUiClosed() }
        for value in ["", "  \n", "custom", ""] {
            local.setTerminalTitle(source:local.terminal,title:value)
            XCTAssertEqual(local.title,value == "custom" ? "custom" : "Alpine")
        }
        ssh.setTerminalTitle(source:ssh.terminal,title:"")
        XCTAssertEqual(ssh.title,"my-server");XCTAssertTrue(local.isLocal);XCTAssertFalse(ssh.isLocal)
    }
    func testNativeTerminalFocusSurvivesRepeatedCanvasUpdates() throws {
        let previous=UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.flatMap(\.windows).first { $0.isKeyWindow }
        let scene=try XCTUnwrap(previous?.windowScene)
        let window=UIWindow(windowScene:scene),controller=UIViewController()
        let session=TerminalSession(title:"Alpine",isLocal:true),model=WorkspaceModel(prepareEnvironment:false)
        defer { session.terminal.updateUiClosed();window.isHidden=true;previous?.makeKeyAndVisible() }
        model.add(session)
        let id=WorkspaceItemID.terminal(session.id)
        let canvas=try WaylandCanvas();canvas.frame=CGRect(x:400,y:0,width:200,height:160)
        session.terminal.frame=CGRect(x:0,y:0,width:320,height:160)
        controller.view.addSubview(session.terminal);controller.view.addSubview(canvas);window.rootViewController=controller;window.makeKeyAndVisible()
        canvas.setKeyboardFocus(true);XCTAssertTrue(canvas.isFirstResponder)
        session.terminal.onFocusRequested={ model.focus(id) }
        session.terminal.setKeyboardFocus(true)
        XCTAssertTrue(session.terminal.isFirstResponder);XCTAssertEqual(model.layout.focused,id)
        for _ in 0..<10 { canvas.setKeyboardFocus(false);canvas.textContext=nil }
        XCTAssertTrue(session.terminal.isFirstResponder);XCTAssertFalse(canvas.isFirstResponder)
    }
    func testLateGuestModeAcknowledgementCannotReopenGraphicsOverSelectedTerminal() throws {
        let model=WorkspaceModel(prepareEnvironment:false),session=TerminalSession(title:"Alpine",isLocal:true)
        defer { session.terminal.updateUiClosed() };model.add(session)
        func snapshot(_ mode:String) throws -> [LocalWaylandWindow] {
            try JSONDecoder().decode([LocalWaylandWindow].self,from:Data("[{\"id\":7,\"connection\":1,\"title\":\"editor\",\"app_id\":\"editor\",\"width\":400,\"height\":300,\"revision\":1,\"mode\":\"\(mode)\",\"bounds\":[0,0,400,300]}]".utf8))
        }
        session.onWaylandChanged?(try snapshot("floating"))
        let graphic=WorkspaceItemID.wayland(session.id,7),terminal=WorkspaceItemID.terminal(session.id)
        model.dock(graphic);model.select(terminal)
        session.onWaylandChanged?(try snapshot("floating"))
        session.onWaylandChanged?(try snapshot("maximized"))
        XCTAssertEqual(model.layout.tabs,[terminal,graphic]);XCTAssertEqual(model.layout.selected,terminal);XCTAssertEqual(model.layout.focused,terminal)
    }
}
