// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
@testable import TramesRemote
private actor Capture {
    var text = ""
    func add(_ data: Data) { text += String(decoding: data, as: UTF8.self); if text.utf8.count > 65536 { text = String(text.suffix(32768)) } }
    func contains(_ value: String) -> Bool { text.contains(value) }
}
final class SSHIntegrationTests: XCTestCase {
    func testOpenSSHPTYResizeEd25519AndSFTPRoundtrip() async throws {
        let env = ProcessInfo.processInfo.environment
        guard let keyPath = env["TRAMES_SSH_TEST_KEY"], let serverKeyPath = env["TRAMES_SSH_TEST_HOST_KEY"], let root = env["TRAMES_SSH_TEST_ROOT"] else {
            throw XCTSkip("Set TRAMES_SSH_TEST_* to run against a real local OpenSSH server")
        }
        let port = Int(env["TRAMES_SSH_TEST_PORT"] ?? "22222")!
        let username = env["TRAMES_SSH_TEST_USER"] ?? "agent"
        let pty = env["TRAMES_SSH_TEST_NO_PTY"] != "1"
        let host = try SSHConfig("Host fixture\nHostName 127.0.0.1\nPort \(port)\nUser \(username)").resolve("fixture")
        let temporary = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: temporary, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: temporary) }
        let known = try KnownHosts(url: temporary.appendingPathComponent("hosts.json"))
        let publicKey = try String(contentsOfFile: serverKeyPath).split(separator: " ").prefix(2).joined(separator: " ")
        do { try known.verify(endpoint: host.endpoint, publicKey: publicKey) }
        catch let challenge as HostKeyChallenge { try known.trust(challenge) }
        let session = SSHSession(), capture = Capture()
        let task = Task {
            try await session.run(host: host, credential: .ed25519(try String(contentsOfFile: keyPath), passphrase: nil), knownHosts: known,
                allocatePTY: pty,
                onChallenge: { _ in XCTFail("pinned key unexpectedly challenged") }, output: { data in await capture.add(data) })
        }
        do {
            try await wait {
                do { try await session.send(Data("printf 'SSH_READY\\n'\n".utf8)); return true } catch { return false }
            }
            try await wait { await capture.contains(pty ? "SSH_READY\r\n" : "SSH_READY\n") }
            if pty {
                try await session.resize(columns: 117, rows: 43)
                try await session.send(Data("stty size\n".utf8))
                try await wait { await capture.contains("43 117") }
            }
            let source = temporary.appendingPathComponent("source"), destination = temporary.appendingPathComponent("copy")
            let bytes = Data((0..<1048576).map { UInt8(truncatingIfNeeded: $0) })
            try bytes.write(to: source)
            let remote = root + "/sftp-" + UUID().uuidString
            try await session.transfer(remote: remote, local: source, upload: true)
            do { try await session.transfer(remote: remote, local: source, upload: true); XCTFail("existing remote file overwritten") }
            catch {}
            try await session.transfer(remote: remote, local: destination, upload: false)
            XCTAssertEqual(try Data(contentsOf: destination),bytes)
            // /dev/zero never reaches EOF: cancellation must remove the native
            // partial copy and leave the separate terminal channel usable.
            let cancelledTarget = temporary.appendingPathComponent("cancelled-copy")
            let download = Task { try await session.transfer(remote: "/dev/zero", local: cancelledTarget, upload: false) }
            do {
                try await wait {
                    let size = try? cancelledTarget.resourceValues(forKeys: [.fileSizeKey]).fileSize
                    return (size ?? 0) > 0
                }
            } catch { download.cancel(); _ = try? await download.value; throw error }
            download.cancel()
            do { try await download.value; XCTFail("cancelled copy continued to completion") } catch {}
            XCTAssertFalse(FileManager.default.fileExists(atPath: cancelledTarget.path))
            try await session.send(Data("printf 'AFTER_SFTP_CANCEL\\n'\n".utf8))
            try await wait { await capture.contains(pty ? "AFTER_SFTP_CANCEL\r\n" : "AFTER_SFTP_CANCEL\n") }
            try await session.send(Data("rm '\(remote)'\nexit\n".utf8))
            try await task.value
            await session.close()
        } catch { await session.close(); task.cancel(); throw error }
    }
    private func wait(_ predicate: () async throws -> Bool) async throws {
        let deadline = Date().addingTimeInterval(15)
        while Date() < deadline {
            if try await predicate() { return }
            try await Task.sleep(nanoseconds: 20_000_000)
        }
        throw TramesError.message("OpenSSH integration timeout")
    }
}
