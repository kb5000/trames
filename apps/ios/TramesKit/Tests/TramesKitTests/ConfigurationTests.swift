// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
@testable import TramesKit
final class ConfigurationTests: XCTestCase {
    func testSSHFirstValuePatternsQuotesNegationAndWarnings() throws {
        let config = try SSHConfig("""
        Host build* !build-old
          User builder
          IdentityFile "~/.ssh/my key" # canonical guest key
        Host build
          HostName 127.0.0.1
          Port=2222
          User ignored
          ProxyJump bastion
          StrictHostKeyChecking no
        Host *
          User fallback
        """)
        let host = try config.resolve("build")
        XCTAssertEqual(host.hostname, "127.0.0.1")
        XCTAssertEqual(host.port, 2222)
        XCTAssertEqual(host.user, "builder")
        XCTAssertEqual(host.identityFile, "~/.ssh/my key")
        XCTAssertEqual(host.warnings.count, 2)
        XCTAssertEqual(try config.resolve("build-old").user, "fallback")
        XCTAssertTrue(config.aliases.contains("build"))
    }
    func testSSHInvalidOrUnsupportedConditionalConfigIsRejected() throws {
        XCTAssertThrowsError(try SSHConfig("Host a\nPort -1").resolve("a"))
        XCTAssertThrowsError(try SSHConfig("Match exec dangerous\nUser root"))
        XCTAssertThrowsError(try SSHConfig("Host x\nUser \"broken"))
        XCTAssertThrowsError(try SSHConfig("Host x\nIdentityFile %d/key").resolve("x"))
        XCTAssertThrowsError(try SSHConfig(String(repeating: "x", count: 65537)))
    }
    func testSSHOptionalEqualsSeparatorPreservesValueEquals() throws {
        for separator in [" ", "=", " =", "= ", " = "] {
            let host = try SSHConfig("Host build\nPort\(separator)2222\nIdentityFile\(separator)\"~/.ssh/key=work\"").resolve("build")
            XCTAssertEqual(host.port, 2222)
            XCTAssertEqual(host.identityFile, "~/.ssh/key=work")
        }
        XCTAssertThrowsError(try SSHConfig("Host build\nPort = =2222").resolve("build"))
    }
    func testHostKeysRequireExplicitTrustAndRejectChangesAfterReload() throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: directory) }
        let url = directory.appendingPathComponent("hosts.json")
        let store = try KnownHosts(url: url)
        let key = "ssh-ed25519 " + Data("test public key".utf8).base64EncodedString()
        do { try store.verify(endpoint: "[host]:22", publicKey: key); XCTFail("unknown host trusted") }
        catch let challenge as HostKeyChallenge {
            XCTAssertTrue(challenge.fingerprint.hasPrefix("SHA256:"))
            try store.trust(challenge)
        }
        try KnownHosts(url: url).verify(endpoint: "[host]:22", publicKey: key)
        XCTAssertThrowsError(try store.verify(endpoint: "[host]:22", publicKey: key+"changed"))
        XCTAssertThrowsError(try store.trust(HostKeyChallenge(endpoint: "[host]:22", publicKey: "changed", fingerprint: "invalid")))
        XCTAssertThrowsError(try store.verify(endpoint: "[host]:23", publicKey: key))
    }
}
