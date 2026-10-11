// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
import TramesKit
@testable import Trames
final class LocalNetworkTests: XCTestCase {
    func testRealAlpineGatewayDNSAndHTTPSOnAppleRuntime() async throws {
        let (archive,response) = try await ConnectivityRetry.download(from: EnvironmentStore.archiveURL)
        defer { try? FileManager.default.removeItem(at: archive) }
        XCTAssertEqual((response as? HTTPURLResponse)?.statusCode,200)
        let root=FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at:root) }
        let session=try LocalSession(store:root,rootfs:archive,total:64<<30,available:32<<30)
        defer {session.close()}
        let began=Date()
        while try session.status().phase == "starting" && Date().timeIntervalSince(began)<120 {
            try await Task.sleep(for:.milliseconds(20))
        }
        XCTAssertEqual(try session.status().phase,"running")
        _ = try session.receive()
        let command="cat /etc/resolv.conf; nslookup -type=A dl-cdn.alpinelinux.org; apk update; printf '\\nNETWORK_RESULT:%d\\n' $?\n"
        XCTAssertEqual(try session.send(Data(command.utf8)),command.utf8.count)
        var output=Data()
        while Date().timeIntervalSince(began)<180 {
            output.append(try session.receive())
            if String(decoding:output,as:UTF8.self).contains("\r\nNETWORK_RESULT:") {break}
            try await Task.sleep(for:.milliseconds(20))
        }
        let text=String(decoding:output,as:UTF8.self)
        XCTAssertTrue(text.contains("nameserver 10.0.0.1"),text)
        XCTAssertTrue(text.contains("Name:\tdl-cdn.alpinelinux.org"),text)
        XCTAssertFalse(text.contains("DNS lookup error"),text)
        XCTAssertFalse(text.contains("Can't find"),text)
        XCTAssertTrue(text.contains("distinct packages available"),text)
        XCTAssertTrue(text.contains("\r\nNETWORK_RESULT:0\r\n"),text)
    }
}
