// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
import Crypto
import NIO
import NIOSSH
public struct HostKeyChallenge: Error, LocalizedError, Identifiable, Sendable {
    public let endpoint: String
    public let publicKey: String
    public let fingerprint: String
    public var id: String { endpoint }
    public var errorDescription: String? { "Unknown SSH host \(endpoint), \(fingerprint). Verify this fingerprint before trusting it." }
}
public final class KnownHosts: @unchecked Sendable {
    private let lock = NSLock()
    private let url: URL
    private var keys: [String:String]
    public init(url: URL) throws {
        self.url = url
        if FileManager.default.fileExists(atPath: url.path) {
            let data = try Data(contentsOf: url)
            guard data.count <= 65536 else { throw TramesError.message("Known hosts file too large") }
            keys = try JSONDecoder().decode([String:String].self, from: data)
        } else { keys = [:] }
    }
    public func verify(endpoint: String, publicKey: String) throws {
        lock.lock(); defer { lock.unlock() }
        if let trusted = keys[endpoint] {
            guard trusted == publicKey else { throw TramesError.message("SSH HOST KEY CHANGED for \(endpoint). Connection refused. Verify the server independently before changing its stored key.") }; return
        }
        let parts = publicKey.split(separator: " ")
        guard parts.count >= 2, let bytes = Data(base64Encoded: String(parts[1])) else { throw TramesError.message("Invalid SSH public key") }
        let fingerprint = "SHA256:" + Data(SHA256.hash(data: bytes)).base64EncodedString().replacingOccurrences(of: "=", with: "")
        throw HostKeyChallenge(endpoint: endpoint, publicKey: publicKey, fingerprint: fingerprint)
    }
    /// Explicit native user action only; a changed key cannot be overwritten here.
    public func trust(_ challenge: HostKeyChallenge) throws {
        lock.lock(); defer { lock.unlock() }
        guard keys[challenge.endpoint] == nil || keys[challenge.endpoint] == challenge.publicKey else { throw TramesError.message("A different key is already trusted for this host") }
        guard keys.count < 256 || keys[challenge.endpoint] != nil else { throw TramesError.message("Known host limit reached") }
        var updated = keys; updated[challenge.endpoint] = challenge.publicKey
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        let data = try JSONEncoder().encode(updated)
        guard data.count <= 65536 else { throw TramesError.message("Known host storage limit reached") }
        try data.write(to: url, options: .atomic)
        try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: url.path); keys = updated
    }
}
final class HostValidator: NIOSSHClientServerAuthenticationDelegate, @unchecked Sendable {
    let knownHosts: KnownHosts, endpoint: String
    let onChallenge: @Sendable (HostKeyChallenge) -> Void
    init(knownHosts: KnownHosts, endpoint: String, onChallenge: @escaping @Sendable (HostKeyChallenge) -> Void) {
        self.knownHosts = knownHosts; self.endpoint = endpoint; self.onChallenge = onChallenge
    }
    func validateHostKey(hostKey: NIOSSHPublicKey, validationCompletePromise: EventLoopPromise<Void>) {
        do { try knownHosts.verify(endpoint: endpoint, publicKey: String(openSSHPublicKey: hostKey)); validationCompletePromise.succeed(()) }
        catch let challenge as HostKeyChallenge { onChallenge(challenge); validationCompletePromise.fail(challenge) }
        catch { validationCompletePromise.fail(error) }
    }
}
