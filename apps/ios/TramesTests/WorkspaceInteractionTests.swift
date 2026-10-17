// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
import UIKit
import SwiftUI
import Combine
import TramesKit
@testable import Trames

@MainActor
final class WorkspaceInteractionTests: XCTestCase {
    func testMultipleAlpineTerminalsShareGuestAndSurviveClosingFirstWindow() async throws {
        let previous=UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.flatMap(\.windows).first { $0.isKeyWindow }
        let window=UIWindow(windowScene:try XCTUnwrap(previous?.windowScene))
        let model=WorkspaceModel(prepareEnvironment:false),first=TerminalSession(title:"Alpine",isLocal:true)
        let directory=FileManager.default.temporaryDirectory.appendingPathComponent("multiple-terminals-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at:directory,withIntermediateDirectories:true)
        let archive=try XCTUnwrap(Bundle.main.resourceURL).appendingPathComponent("GuestVulkan/acceptance/alpine-minirootfs.tar.gz")
        let native=try LocalSession(store:directory.appendingPathComponent("store"),rootfs:archive,total:1<<30,available:1<<30,network:"off",multipleTerminals:true)
        var created:[TerminalSession]=[first],output:[UUID:String]=[:]
        let defaults=UserDefaults.standard,oldBridge=defaults.object(forKey:"bridgeEnabled")
        let pasteboard=UIPasteboard.general
        defaults.set(true,forKey:"bridgeEnabled")
        defer {
            for session in created {session.close();session.terminal.updateUiClosed()}
            native.close();window.isHidden=true;previous?.makeKeyAndVisible();pasteboard.items=[]
            if let oldBridge {defaults.set(oldBridge,forKey:"bridgeEnabled")} else {defaults.removeObject(forKey:"bridgeEnabled")}
            try? FileManager.default.removeItem(at:directory)
        }
        var stage="start three PTYs"
        func wait(_ condition:() throws -> Bool) async throws {
            let deadline=Date().addingTimeInterval(25)
            while try !condition() {
                guard Date()<deadline else {
                    let detail="Multi-terminal wait timed out at \(stage): \(output); states=\(created.map(\.state))"
                    print("MULTI FAILURE: \(detail)")
                    throw NSError(domain:"TramesTerminalAcceptance",code:1,userInfo:[NSLocalizedDescriptionKey:detail])
                }
                try await Task.sleep(for:.milliseconds(20))
            }
        }
        first.local=native;first.bridge.attach(native);model.add(first);first.startPolling()
        model.openLocal();model.openLocal();created=model.sessions
        XCTAssertEqual(created.count,3)
        for session in created {
            session.onTerminalOutput={ output[session.id,default:""]+=String(decoding:$0,as:UTF8.self) }
            // This check exercises workspace/PTY routing. The soft keyboard
            // and native composition already have separate acceptance; keeping
            // it hidden also keeps the three viewport measurements stable.
            session.terminal.keyboardVisibility={false}
        }
        try await wait { try created.allSatisfy { session in
            guard let id=session.terminalID else {return false}
            return try native.terminalStatus(id).phase == "running"
        } }
        XCTAssertEqual(Set(created.compactMap(\.terminalID)).count,3)
        XCTAssertTrue(created.allSatisfy{$0.local === native})
        print("MULTI: three independent PTYs are running")
        let second=created[1],third=created[2]
        let host=UIHostingController(rootView:WorkspaceView(workspace:model))
        window.rootViewController=host;window.makeKeyAndVisible()
        host.view.frame=CGRect(x:0,y:0,width:1000,height:700)
        model.select(.terminal(first.id));model.detach(.terminal(second.id));model.detach(.terminal(third.id))
        model.saveFrame(.terminal(second.id),DisplayWindowGeometry(origin:CGPoint(x:30,y:30),size:CGSize(width:390,height:310)))
        model.saveFrame(.terminal(third.id),DisplayWindowGeometry(origin:CGPoint(x:440,y:100),size:CGSize(width:490,height:360)))
        try await Task.sleep(for:.milliseconds(300))
        for session in created {
            stage="viewport \(session.title)"
            let terminal=session.terminal.getTerminal()
            let marker="GRID_\(session.id.uuidString.prefix(6)):"
            session.send(Data("stty -echo; printf '\\n\(marker)'; stty size\n".utf8))
            let expected="\r\n\(marker)\(terminal.rows) \(terminal.cols)\r\n"
            try await wait {output[session.id,default:""].contains(expected)}
        }
        XCTAssertNotEqual(second.terminal.getTerminal().cols,third.terminal.getTerminal().cols)
        print("MULTI: independent workspace viewports and real stty sizes passed")
        stage="shared files and private cwd/env"
        first.send(Data("cd /tmp; export PRIVATE=first; printf shared >/tmp/multi-shared; printf '\\nFIRST_READY\\n'\n".utf8))
        try await wait {output[first.id,default:""].contains("\r\nFIRST_READY\r\n")}
        second.send(Data("printf '\\nSHARED:%s:%s:%s\\n' \"$(cat /tmp/multi-shared)\" \"${PRIVATE-unset}\" \"$(pwd)\"\n".utf8))
        try await wait {output[second.id,default:""].contains("\r\nSHARED:shared:unset:/root\r\n")}
        XCTAssertFalse(output[first.id,default:""].contains("SHARED:shared"))
        model.minimize(.terminal(second.id));model.restore(.terminal(second.id));model.dock(.terminal(second.id))
        XCTAssertEqual(model.layout.placement(.terminal(second.id)),.tabbed)
        model.close(first)
        print("MULTI: shared files, private cwd/env and docking passed; first view closed")
        XCTAssertTrue(model.localOwner === first);XCTAssertFalse(model.sessions.contains{$0 === first})
        // The invisible environment owner must still poll the Bridge exactly
        // once; a secondary shell can copy to the actual iPad pasteboard.
        let json="{\"version\":1,\"id\":1,\"method\":\"clipboard.write\",\"params\":{\"text\":\"multiple terminals 中文\"}}"
        stage="Bridge after closing first view"
        third.send(Data("exec 3<>/dev/trames-bridge; printf '%s\\n' '\(json)' >&3; IFS= read -r reply <&3; printf '\\nBRIDGE_FROM_THIRD\\n'; exec 3>&-\n".utf8))
        try await wait {output[third.id,default:""].contains("\r\nBRIDGE_FROM_THIRD\r\n")}
        XCTAssertEqual(pasteboard.string,"multiple terminals 中文")
        stage="shared pause/resume"
        model.background();third.send(Data("printf '\\nAFTER_SHARED_RESUME\\n'\n".utf8));model.foreground()
        try await wait {output[third.id,default:""].contains("\r\nAFTER_SHARED_RESUME\r\n")}
        model.close(second);model.close(third)
        XCTAssertTrue(model.sessions.isEmpty)
        model.openLocal();let reopened=try XCTUnwrap(model.sessions.first);created.append(reopened)
        reopened.terminal.keyboardVisibility={false}
        stage="reopen after closing all views"
        reopened.onTerminalOutput={output[reopened.id,default:""]+=String(decoding:$0,as:UTF8.self)}
        try await wait {reopened.terminalID != nil}
        reopened.send(Data("stty -echo; printf '\\nREOPENED:'; cat /tmp/multi-shared; printf '\\n'\n".utf8))
        try await wait {output[reopened.id,default:""].contains("\r\nREOPENED:shared\r\n")}
        XCTAssertTrue(reopened.local === native)
        print("MULTI: Bridge, pause/resume and reopen after closing all views passed")
    }
    func testSystemAppWindowResizeUpdatesTerminalAndGuestPTY() async throws {
        let previous=UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.flatMap(\.windows).first { $0.isKeyWindow }
        let window=UIWindow(windowScene:try XCTUnwrap(previous?.windowScene)),parent=UIViewController()
        let model=WorkspaceModel(prepareEnvironment:false),session=TerminalSession(title:"Alpine",isLocal:true)
        let directory=FileManager.default.temporaryDirectory.appendingPathComponent("system-resize-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at:directory,withIntermediateDirectories:true)
        let archive=try XCTUnwrap(Bundle.main.resourceURL).appendingPathComponent("GuestVulkan/acceptance/alpine-minirootfs.tar.gz")
        let native=try LocalSession(store:directory.appendingPathComponent("store"),rootfs:archive,total:1<<30,available:1<<30,network:"off")
        session.local=native;model.add(session)
        defer {
            session.close();session.terminal.updateUiClosed();window.isHidden=true;previous?.makeKeyAndVisible()
            try? FileManager.default.removeItem(at:directory)
        }
        let started=Date()
        while try native.status().phase != "running" {
            let status=try native.status()
            if let error=status.error {throw TramesError.message(error)}
            guard Date().timeIntervalSince(started)<30 else {throw TramesError.message("Resize test shell did not start")}
            try await Task.sleep(for:.milliseconds(20))
        }
        let host=WorkspaceKeyboardController(workspace:model)
        window.rootViewController=parent
        parent.addChild(host);parent.view.addSubview(host.view);host.didMove(toParent:parent)
        window.makeKeyAndVisible()
        let terminal=session.terminal
        var previousColumns:Int?
        for (index,size) in [CGSize(width:360,height:430),CGSize(width:750,height:980),CGSize(width:500,height:600),CGSize(width:750,height:980)].enumerated() {
            // Resize the native app-container view directly. No docking, model
            // change or TerminalPane.updateUIView call drives this transition.
            host.view.frame=CGRect(origin:.zero,size:size);host.view.setNeedsLayout();host.view.layoutIfNeeded()
            let deadline=Date().addingTimeInterval(5)
            while (abs(terminal.bounds.width-size.width)>1 || terminal.getTerminal().cols==previousColumns) && Date()<deadline {
                try await Task.sleep(for:.milliseconds(20))
            }
            let columns=terminal.getTerminal().cols,rows=terminal.getTerminal().rows
            XCTAssertEqual(terminal.bounds.width,size.width,accuracy:1)
            XCTAssertNotEqual(columns,previousColumns);XCTAssertGreaterThan(rows,1)
            XCTAssertLessThanOrEqual(terminal.getOptimalFrameSize().width,terminal.bounds.width)
            let command=Data("printf '\\nSYS_GRID_\(index):'; stty size\n".utf8)
            XCTAssertEqual(try native.send(command),command.count)
            let expected="\r\nSYS_GRID_\(index):\(rows) \(columns)\r\n"
            var output="";let received=Date().addingTimeInterval(10)
            while !output.contains(expected) && Date()<received {
                output+=String(decoding:try native.receive(),as:UTF8.self)
                try await Task.sleep(for:.milliseconds(20))
            }
            XCTAssertTrue(output.contains(expected),"Guest PTY did not match the actual viewport: \(output)")
            previousColumns=columns
        }
    }
    func testGraphicsPreferredFrameSurvivesBackgroundAndTransientViewportChanges() async throws {
        let previous=UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.flatMap(\.windows).first { $0.isKeyWindow }
        let window=UIWindow(windowScene:try XCTUnwrap(previous?.windowScene))
        let model=WorkspaceModel(prepareEnvironment:false),session=TerminalSession(title:"Alpine",isLocal:true)
        defer {session.terminal.updateUiClosed();window.isHidden=true;previous?.makeKeyAndVisible()}
        model.add(session)
        func status(_ revision:Int) throws -> LocalWaylandStatus {
            try JSONDecoder().decode(LocalWaylandStatus.self,from:Data("""
            {"revision":\(revision),"text":null,"windows":[{"id":7,"connection":1,"title":"vkcube","app_id":"vkcube","width":430,"height":296,"revision":\(revision),"mode":"floating","bounds":[0,0,430,296]}]}
            """.utf8))
        }
        session.applyWaylandStatus(try status(1))
        let id=WorkspaceItemID.wayland(session.id,7)
        let saved=DisplayWindowGeometry(origin:CGPoint(x:310,y:280),size:CGSize(width:430,height:340))
        model.saveFrame(id,saved)
        let original=CGSize(width:1024,height:768)
        let host=UIHostingController(rootView:WorkspaceWindow(workspace:model,item:id,viewport:original))
        host.safeAreaRegions = .container
        window.rootViewController=host;window.makeKeyAndVisible()
        func canvas(_ view:UIView)->WaylandCanvas? {
            if let view=view as? WaylandCanvas {return view}
            return view.subviews.lazy.compactMap {canvas($0)}.first
        }
        try await Task.sleep(for:.milliseconds(100))
        let view=try XCTUnwrap(canvas(host.view)),before=view.convert(view.bounds,to:host.view)
        for revision in 2...4 {
            model.background()
            for size in [CGSize(width:250,height:190),CGSize(width:600,height:350)] {
                model.updateViewport(size)
                host.rootView=WorkspaceWindow(workspace:model,item:id,viewport:size)
                try await Task.sleep(for:.milliseconds(50))
                XCTAssertEqual(model.layout.frames[id],saved)
            }
            session.applyWaylandStatus(try status(revision))
            model.foreground();model.updateViewport(original)
            host.rootView=WorkspaceWindow(workspace:model,item:id,viewport:original)
            try await Task.sleep(for:.milliseconds(100))
            XCTAssertTrue(canvas(host.view) === view)
            let after=view.convert(view.bounds,to:host.view)
            XCTAssertEqual(after.minX,before.minX,accuracy:1);XCTAssertEqual(after.minY,before.minY,accuracy:1)
            XCTAssertEqual(after.width,before.width,accuracy:1);XCTAssertEqual(after.height,before.height,accuracy:1)
            XCTAssertEqual(model.layout.frames[id],saved)
        }
    }
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
    func testOnlyTerminalCanMinimizeWithoutReactivatingItsHiddenKeyboard() async throws {
        let previous=UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.flatMap(\.windows).first { $0.isKeyWindow }
        let window=UIWindow(windowScene:try XCTUnwrap(previous?.windowScene))
        let model=WorkspaceModel(prepareEnvironment:false),session=TerminalSession(title:"Alpine",isLocal:true)
        defer { session.terminal.updateUiClosed();window.isHidden=true;previous?.makeKeyAndVisible() }
        model.add(session)
        let id=WorkspaceItemID.terminal(session.id)
        window.rootViewController=UIHostingController(rootView:WorkspaceView(workspace:model));window.makeKeyAndVisible()
        func settle(_ condition:()->Bool) async throws {
            let deadline=Date().addingTimeInterval(5)
            while !condition() && Date()<deadline { try await Task.sleep(for:.milliseconds(20)) }
            XCTAssertTrue(condition())
        }
        try await settle { session.terminal.isFirstResponder }
        let parent=try XCTUnwrap(session.terminal.superview)
        for _ in 0..<3 {
            model.detach(id)
            try await settle { model.layout.placement(id) == .floating && session.terminal.isFirstResponder }
            // Model a late queued keyboard request from the pre-minimize layout.
            session.terminal.setKeyboardFocus(true,deferred:true)
            model.minimize(id)
            try await settle { !session.terminal.isFirstResponder }
            XCTAssertNil(model.layout.selected);XCTAssertNil(model.layout.focused)
            XCTAssertEqual(model.layout.tabs,[id]);XCTAssertEqual(model.layout.placement(id),.minimized)
            XCTAssertTrue(session.terminal.superview === parent)
            XCTAssertFalse(session.terminal.canBecomeFirstResponder)
            XCTAssertFalse(session.terminal.becomeFirstResponder())
            model.select(id)
            try await settle { session.terminal.isFirstResponder }
            XCTAssertEqual(model.layout.selected,id);XCTAssertTrue(session.terminal.superview === parent)
        }
        session.terminal.resignFirstResponder()
        session.terminal.setKeyboardFocus(true,deferred:true)
        try await Task.sleep(for:.milliseconds(50))
        XCTAssertFalse(session.terminal.isFirstResponder,"A view redraw must preserve manual keyboard dismissal")
    }
    func testDockingTerminalReflowsTheGrid() async throws {
        let previous=UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.flatMap(\.windows).first { $0.isKeyWindow }
        let window=UIWindow(windowScene:try XCTUnwrap(previous?.windowScene))
        let model=WorkspaceModel(prepareEnvironment:false),session=TerminalSession(title:"Alpine",isLocal:true)
        defer {session.terminal.updateUiClosed();window.isHidden=true;previous?.makeKeyAndVisible()}
        model.add(session);let id=WorkspaceItemID.terminal(session.id)
        window.rootViewController=UIHostingController(rootView:WorkspaceView(workspace:model));window.makeKeyAndVisible()
        try await Task.sleep(for:.milliseconds(200))
        let originalColumns=session.terminal.getTerminal().cols
        model.detach(id)
        model.saveFrame(id,DisplayWindowGeometry(origin:CGPoint(x:10,y:10),size:CGSize(width:320,height:300)))
        try await Task.sleep(for:.milliseconds(150))
        let floatingColumns=session.terminal.getTerminal().cols
        let floatingFrame=session.terminal.convert(session.terminal.bounds,to:window)
        XCTAssertLessThan(floatingColumns,originalColumns)
        var grids:[(Int,Int)]=[]
        session.terminal.onGridLayout={grids.append(($0,$1))}
        model.dock(id)
        let deadline=Date().addingTimeInterval(5)
        while (session.terminal.getTerminal().cols<=floatingColumns || grids.isEmpty) && Date()<deadline {try await Task.sleep(for:.milliseconds(20))}
        XCTAssertGreaterThan(session.terminal.getTerminal().cols,floatingColumns)
        XCTAssertGreaterThan(session.terminal.bounds.width,floatingFrame.width)
        XCTAssertEqual(grids.last?.0,session.terminal.getTerminal().cols)
        XCTAssertEqual(grids.last?.1,session.terminal.getTerminal().rows)
        XCTAssertLessThanOrEqual(session.terminal.getOptimalFrameSize().width,session.terminal.bounds.width)
    }
    func testTransientDialogFrameFitsClientAndSurvivesViewportChange() throws {
        let model=WorkspaceModel(prepareEnvironment:false),session=TerminalSession(title:"Alpine",isLocal:true)
        defer {session.terminal.updateUiClosed()};model.add(session)
        let data=Data("""
        [{"id":7,"connection":1,"title":"editor","app_id":"editor","width":800,"height":600,"revision":1,"mode":"floating","bounds":[0,0,800,600]},
         {"id":8,"connection":1,"title":"Save changes?","app_id":"editor","width":360,"height":160,"revision":1,"mode":"floating","bounds":[0,0,360,160],"parent":7,"min_size":[360,160],"max_size":[360,160]}]
        """.utf8)
        let windows=try JSONDecoder().decode([LocalWaylandWindow].self,from:data)
        session.waylandWindows=windows;session.onWaylandChanged?(windows)
        let id=WorkspaceItemID.wayland(session.id,8)
        XCTAssertEqual(model.layout.frames[id]?.size,CGSize(width:360,height:204))
        model.updateViewport(CGSize(width:700,height:600))
        XCTAssertEqual(model.layout.frames[id]?.size,CGSize(width:360,height:204))
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
