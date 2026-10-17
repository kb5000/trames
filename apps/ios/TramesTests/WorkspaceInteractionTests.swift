// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
import UIKit
import SwiftUI
import Combine
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
        for epoch in 1...10 {
            canvas.setKeyboardFocus(false)
            let context=Data("{\"id\":11,\"epoch\":\(epoch),\"version\":1,\"surface\":7,\"window\":7,\"serial\":2,\"text\":\"Numeric\",\"cursor\":7,\"anchor\":7,\"caret\":[0,0,1,20],\"hint\":0,\"purpose\":3}".utf8)
            canvas.textContext=try JSONDecoder().decode(LocalWaylandText.self,from:context)
            XCTAssertTrue(session.terminal.isFirstResponder)
        }
        canvas.textContext=nil
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
    func testFrameRevisionsDoNotRebuildTheWorkspaceLayout() throws {
        let session=TerminalSession(title:"Alpine",isLocal:true)
        var changes=0
        let observation=session.$waylandWindows.dropFirst().sink { _ in changes += 1 }
        defer { observation.cancel();session.terminal.updateUiClosed() }
        func status(_ revision: Int, width: Int = 400) throws -> LocalWaylandStatus {
            try JSONDecoder().decode(LocalWaylandStatus.self,from:Data("""
            {"revision":\(revision),"text":null,"windows":[{"id":7,"connection":1,"title":"cube","app_id":"cube","width":\(width),"height":300,"revision":\(revision),"mode":"floating","bounds":[0,0,\(width),300]}]}
            """.utf8))
        }
        for revision in 1...100 { session.applyWaylandStatus(try status(revision)) }
        XCTAssertEqual(changes,1)
        session.applyWaylandStatus(try status(101,width:500))
        XCTAssertEqual(changes,2);XCTAssertEqual(session.waylandWindows[0].width,500)
    }
    func testSameLayoutModeAcknowledgementDoesNotMaskLaterGuestRequests() throws {
        let model=WorkspaceModel(prepareEnvironment:false),session=TerminalSession(title:"Alpine",isLocal:true)
        defer { session.terminal.updateUiClosed() };model.add(session)
        func update(_ revision: Int, _ mode: String) throws {
            let data=Data("""
            {"revision":\(revision),"text":null,"windows":[{"id":7,"connection":1,"title":"cube","app_id":"cube","width":400,"height":300,"revision":\(revision),"mode":"\(mode)","bounds":[0,0,400,300]}]}
            """.utf8)
            session.applyWaylandStatus(try JSONDecoder().decode(LocalWaylandStatus.self,from:data))
        }
        try update(1,"maximized")
        let id=WorkspaceItemID.wayland(session.id,7)
        model.minimize(id);model.restore(id)
        try update(2,"maximized")
        try update(3,"floating")
        XCTAssertEqual(model.layout.placement(id),.floating)
        XCTAssertFalse(model.layout.tabs.contains(id))
    }
    func testTerminalKeepsTheSameUIKitHostAcrossDockDetachAndBackgroundTabs() async throws {
        let previous=UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.flatMap(\.windows).first { $0.isKeyWindow }
        let window=UIWindow(windowScene:try XCTUnwrap(previous?.windowScene))
        let model=WorkspaceModel(prepareEnvironment:false)
        let session=TerminalSession(title:"Alpine",isLocal:true),other=TerminalSession(title:"SSH")
        defer {
            session.terminal.updateUiClosed();other.terminal.updateUiClosed()
            window.isHidden=true;previous?.makeKeyAndVisible()
        }
        model.add(session);model.add(other)
        let id=WorkspaceItemID.terminal(session.id)
        model.select(id)
        window.rootViewController=UIHostingController(rootView:WorkspaceView(workspace:model))
        window.makeKeyAndVisible()
        try await Task.sleep(for:.milliseconds(250))
        let parent=try XCTUnwrap(session.terminal.superview)
        for _ in 0..<5 {
            model.detach(id);try await Task.sleep(for:.milliseconds(50))
            XCTAssertTrue(session.terminal.superview === parent)
            model.dock(id);try await Task.sleep(for:.milliseconds(50))
            XCTAssertTrue(session.terminal.superview === parent)
            model.select(.terminal(other.id));try await Task.sleep(for:.milliseconds(50))
            XCTAssertTrue(session.terminal.superview === parent)
            model.select(id)
        }
        session.terminal.feed(text:"terminal survived\r\n")
        XCTAssertTrue(session.terminal.window === window)
        XCTAssertGreaterThan(session.terminal.getTerminal().cols,1)
        XCTAssertGreaterThan(session.terminal.getTerminal().rows,1)
    }
    func testCanvasFollowsHostContentAndFinalResizeRequest() async throws {
        let previous=UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.flatMap(\.windows).first { $0.isKeyWindow }
        let window=UIWindow(windowScene:try XCTUnwrap(previous?.windowScene))
        let model=WorkspaceModel(prepareEnvironment:false),session=TerminalSession(title:"Alpine",isLocal:true)
        defer { session.terminal.updateUiClosed();window.isHidden=true;previous?.makeKeyAndVisible() }
        model.add(session)
        let status=try JSONDecoder().decode(LocalWaylandStatus.self,from:Data("""
        {"revision":1,"text":null,"windows":[{"id":7,"connection":1,"title":"cube","app_id":"cube","width":1024,"height":768,"revision":1,"mode":"floating","bounds":[-32,-32,1088,832]}]}
        """.utf8))
        session.applyWaylandStatus(status)
        var sizes:[CGSize]=[]
        session.onWaylandCommand={ command in
            if command["op"] as? String == "resize",let w=command["width"] as? Int,let h=command["height"] as? Int {
                sizes.append(CGSize(width:CGFloat(w),height:CGFloat(h)))
            }
        }
        func waitForResize(_ expected: CGSize) async throws {
            let deadline=Date().addingTimeInterval(5)
            while sizes.last != expected && Date() < deadline {
                try await Task.sleep(for:.milliseconds(20))
            }
            XCTAssertEqual(sizes.last,expected,"The final configure must follow the host content rectangle")
        }
        window.rootViewController=UIHostingController(rootView:WorkspaceView(workspace:model))
        window.makeKeyAndVisible()
        try await Task.sleep(for:.milliseconds(250))
        func findCanvas(_ view:UIView)->WaylandCanvas? {
            if let canvas=view as? WaylandCanvas { return canvas }
            return view.subviews.lazy.compactMap { findCanvas($0) }.first
        }
        let canvas=try XCTUnwrap(findCanvas(try XCTUnwrap(window.rootViewController?.view)))
        let id=WorkspaceItemID.wayland(session.id,7)
        model.saveFrame(id,DisplayWindowGeometry(origin:CGPoint(x:20,y:20),size:CGSize(width:400,height:300)))
        try await waitForResize(CGSize(width:400,height:256))
        XCTAssertEqual(canvas.bounds.width,400,accuracy:1)
        XCTAssertEqual(canvas.bounds.height,256,accuracy:1)
        XCTAssertEqual(sizes.last?.width,400);XCTAssertEqual(sizes.last?.height,256)
        for width in [350,320,280] {
            model.saveFrame(id,DisplayWindowGeometry(origin:CGPoint(x:20,y:20),size:CGSize(width:CGFloat(width),height:260)))
            try await Task.sleep(for:.milliseconds(10))
        }
        try await waitForResize(CGSize(width:280,height:216))
        XCTAssertEqual(canvas.bounds.width,280,accuracy:1);XCTAssertEqual(sizes.last?.width,280)
        XCTAssertEqual(canvas.bounds.height,216,accuracy:1);XCTAssertEqual(sizes.last?.height,216)
        model.dock(id);try await Task.sleep(for:.milliseconds(150))
        XCTAssertTrue(findCanvas(try XCTUnwrap(window.rootViewController?.view)) === canvas)
        XCTAssertGreaterThan(canvas.bounds.width,400)
        model.select(.terminal(session.id));try await Task.sleep(for:.milliseconds(50))
        XCTAssertFalse(canvas.presentationEnabled)
        model.select(id);try await Task.sleep(for:.milliseconds(50))
        XCTAssertTrue(canvas.presentationEnabled)
    }
}
