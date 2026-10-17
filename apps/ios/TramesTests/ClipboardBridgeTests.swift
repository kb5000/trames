// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
import UIKit
import TramesKit
@testable import Trames

@MainActor
final class ClipboardBridgeTests:XCTestCase {
    func testGuestClipboardRequestsAutomaticallyReachTheSystemPasteboard() async throws {
        let defaults=UserDefaults.standard,previous=defaults.object(forKey:"bridgeEnabled")
        defaults.set(true,forKey:"bridgeEnabled")
        let pasteboard=UIPasteboard.general,oldItems=pasteboard.items
        let directory=FileManager.default.temporaryDirectory.appendingPathComponent("clipboard-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at:directory,withIntermediateDirectories:true)
        let archive=try XCTUnwrap(Bundle.main.resourceURL).appendingPathComponent("GuestVulkan/acceptance/alpine-minirootfs.tar.gz")
        let native=try LocalSession(store:directory.appendingPathComponent("store"),rootfs:archive,total:1<<30,available:1<<30,network:"off")
        let controller=BridgeController();controller.attach(native)
        defer {
            controller.close();native.close();pasteboard.items=oldItems
            if let previous { defaults.set(previous,forKey:"bridgeEnabled") } else { defaults.removeObject(forKey:"bridgeEnabled") }
            try? FileManager.default.removeItem(at:directory)
        }
        let started=Date()
        while try native.status().phase != "running" {
            if let error=try native.status().error { throw TramesError.message(error) }
            guard Date().timeIntervalSince(started)<30 else { throw TramesError.message("Clipboard test shell did not start") }
            try await Task.sleep(for:.milliseconds(20))
        }
        let expected="mousepad 剪贴板 🧪"
        pasteboard.string="previous host clipboard"
        for (index,method,params) in [(1,"clipboard.write",["text":expected]),(2,"clipboard.read",[:])] {
            let request:[String:Any] = ["version":1,"id":index,"method":method,"params":params]
            let json=String(decoding:try JSONSerialization.data(withJSONObject:request,options:.sortedKeys),as:UTF8.self)
            let command=Data("exec 3<>/dev/trames-bridge; printf '%s\\n' '\(json)' >&3; IFS= read -r reply <&3; printf '\\nCLIP_REPLY_\(index):%s\\n' \"$reply\"; exec 3>&-\n".utf8)
            XCTAssertEqual(try native.send(command),command.count)
            let deadline=Date().addingTimeInterval(10);var output=""
            let marker="\r\nCLIP_REPLY_\(index):"
            while !output.contains(marker) && Date()<deadline {
                try controller.poll()
                XCTAssertNil(controller.challenge,"Clipboard requests must not open an extra App confirmation")
                output+=String(decoding:try native.receive(),as:UTF8.self)
                try await Task.sleep(for:.milliseconds(20))
            }
            let range=try XCTUnwrap(output.range(of:marker),output)
            let line=String(output[range.upperBound...].prefix(while:{$0 != "\r" && $0 != "\n"}))
            let reply=try XCTUnwrap(JSONSerialization.jsonObject(with:Data(line.utf8)) as? [String:Any])
            XCTAssertNil(reply["error"],line)
            XCTAssertEqual(pasteboard.string,expected)
            let result=try XCTUnwrap(reply["result"] as? [String:Any])
            if method == "clipboard.write" { XCTAssertEqual(result["written"] as? Bool,true) }
            else { XCTAssertEqual(result["text"] as? String,expected) }
        }
    }
}
