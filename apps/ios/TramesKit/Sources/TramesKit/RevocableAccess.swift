// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
/// Revocation cancels active Rust copies and is checked between provider chunks.
public final class RevocableAccess: @unchecked Sendable {
    private let lock = NSLock()
    private var valid = true
    private var handlers: [UUID:@Sendable () -> Void] = [:]
    public init() {}
    public func check() throws {
        lock.lock(); defer { lock.unlock() }
        if !valid { throw BridgeFailure(code: "revoked", message: "Folder authorization was revoked") }
    }
    public func register(_ handler: @escaping @Sendable () -> Void) -> UUID {
        let id = UUID()
        lock.lock(); let active = valid
        if active { handlers[id] = handler }; lock.unlock()
        if !active { handler() }; return id
    }
    public func unregister(_ id: UUID) { lock.lock(); handlers.removeValue(forKey: id); lock.unlock() }
    public func revoke() {
        lock.lock(); valid = false; let callbacks = Array(handlers.values); handlers.removeAll(); lock.unlock()
        for callback in callbacks { callback() }
    }
}
