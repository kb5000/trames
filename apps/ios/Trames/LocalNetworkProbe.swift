// SPDX-License-Identifier: GPL-3.0-or-later
#if DEBUG
import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif
import TramesKit
/// Simulator acceptance invokes the app's linked runtime, avoiding a second
/// copy of TramesKit/SSH/crypto in the hosted test bundle.
enum LocalNetworkProbe {
    static func run() async throws -> String {
        let (archive,response)=try await ConnectivityRetry.download(from:EnvironmentStore.archiveURL)
        defer {try? FileManager.default.removeItem(at:archive)}
        guard (response as? HTTPURLResponse)?.statusCode==200 else {
            throw TramesError.message("Invalid Alpine download response")
        }
        let root=FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        let session=try LocalSession(store:root,rootfs:archive,total:64<<30,available:32<<30)
        do {
            let text=try await collect(session)
            session.close()
            try await LocalSession.reset(store:root)
            return text
        } catch {
            session.close()
            try? await LocalSession.reset(store:root)
            throw error
        }
    }
    private static func collect(_ session:LocalSession) async throws -> String {
        let began=Date()
        while try session.status().phase=="starting" && Date().timeIntervalSince(began)<120 {
            try await Task.sleep(for:.milliseconds(20))
        }
        guard try session.status().phase=="running" else {
            throw TramesError.message("Alpine network probe did not start")
        }
        _ = try session.receive()
        let command="cat /etc/resolv.conf; nslookup -type=A dl-cdn.alpinelinux.org; apk update; printf '\\nNETWORK_RESULT:%d\\n' $?\n"
        guard try session.send(Data(command.utf8))==command.utf8.count else {
            throw TramesError.message("Alpine network probe input was incomplete")
        }
        var output=Data()
        // Both full upstream indexes require ~1.7 billion interpreted
        // instructions; this is deliberately longer than a small TLS fixture.
        while Date().timeIntervalSince(began)<600 {
            let status=try session.status()
            guard status.phase=="running" else {
                throw TramesError.message(status.error ?? "Alpine network probe stopped: \(status.phase)")
            }
            output.append(try session.receive())
            if String(decoding:output,as:UTF8.self).contains("\r\nNETWORK_RESULT:") {break}
            try await Task.sleep(for:.milliseconds(20))
        }
        let text=String(decoding:output,as:UTF8.self)
        guard text.contains("\r\nNETWORK_RESULT:") else {
            throw TramesError.message("Alpine network probe timed out after 600 seconds: \(text.suffix(2048))")
        }
        return text
    }
}
#endif
