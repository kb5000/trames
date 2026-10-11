// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
import CTrames
public struct LocalStatus: Decodable, Sendable, Equatable {
    public let phase: String
    public let error: String?
    public let exit_code: UInt8?
    public let budget_bytes: UInt64
    public let used_bytes: UInt64
    public let available_bytes: UInt64
    public let volume_total_bytes: UInt64
    public let node_limit: UInt64
    public let nodes_used: UInt64
}
/// Methods are serialized by the frontend; the ABI itself is thread-safe.
public final class LocalSession: @unchecked Sendable {
    private let lock = NSLock()
    private var handle: UInt64
    public static var cppAvailable: Bool { trames_execution_backend_available(1) == 1 }
    public init(store: URL, rootfs: URL?, total: UInt64, available: UInt64, network: String = "outbound", useVolumeBudget: Bool = false, useCppBackend: Bool = false) throws {
        guard trames_abi_version() == 2 else { throw TramesError.message("Unsupported Trames ABI") }
        var options: [String:Any] = ["store": store.path, "volume_total":total, "volume_available":available, "network":network]
        options["backend"] = useCppBackend ? "cpp" : "reference"
        if let rootfs { options["rootfs"] = rootfs.path }
        options["budget_bytes"] = useVolumeBudget ? total : UInt64(16) << 30
        let data = try JSONSerialization.data(withJSONObject: options)
        handle = data.withUnsafeBytes { trames_session_open($0.bindMemory(to: UInt8.self).baseAddress, UInt32($0.count)) }
        guard handle != 0 else { throw Self.lastError() }
    }
    deinit { close() }
    public static func reset(store: URL) async throws {
        let data=try JSONSerialization.data(withJSONObject:["store":store.path])
        let began=Date()
        while true {
            let result = await Task.detached(priority: .userInitiated) {
                let value=data.withUnsafeBytes { trames_environment_reset($0.bindMemory(to:UInt8.self).baseAddress,UInt32($0.count)) }
                if value < 0 { return Result<Int32,Error>.failure(lastError()) }
                return Result<Int32,Error>.success(value)
            }.value
            if try result.get() == 0 { return }
            guard Date().timeIntervalSince(began)<10 else {throw TramesError.message("Linux 环境仍在关闭，请稍后重试")}
            try await Task.sleep(for:.milliseconds(20))
        }
    }
    public static func install(store: URL, rootfs: URL, total: UInt64, available: UInt64) throws {
        let data = try JSONSerialization.data(withJSONObject: ["store":store.path, "rootfs":rootfs.path, "volume_total":total, "volume_available":available])
        let result = data.withUnsafeBytes { trames_environment_install($0.bindMemory(to: UInt8.self).baseAddress, UInt32($0.count)) }
        if result < 0 { throw lastError() }
    }
    public func close() {
        lock.lock(); defer { lock.unlock() }
        if handle != 0 { _ = trames_session_close(handle); handle = 0 }
    }
    func currentHandle() throws -> UInt64 {
        lock.lock(); defer { lock.unlock() }
        guard handle != 0 else { throw TramesError.message("Local session closed") }; return handle
    }
    public func send(_ data: Data) throws -> Int {
        guard data.count <= 65536 else { throw TramesError.message("Input chunk exceeds 64 KiB") }
        let h = try currentHandle()
        let count = data.withUnsafeBytes { trames_session_input(h, $0.bindMemory(to: UInt8.self).baseAddress, UInt32($0.count)) }
        guard count >= 0 else { throw Self.lastError() }; return Int(count)
    }
    public func receive() throws -> Data {
        let h = try currentHandle()
        var data = Data(count: 65536)
        let count = data.withUnsafeMutableBytes { trames_session_output(h, $0.bindMemory(to: UInt8.self).baseAddress, UInt32($0.count)) }
        guard count >= 0 else { throw Self.lastError() }; data.count = Int(count); return data
    }
    public func status() throws -> LocalStatus {
        let h = try currentHandle()
        var data = Data(count: 8192)
        let count = data.withUnsafeMutableBytes { trames_session_status(h, $0.bindMemory(to: UInt8.self).baseAddress, UInt32($0.count)) }
        guard count >= 0 else { throw Self.lastError() }; data.count = Int(count)
        return try JSONDecoder().decode(LocalStatus.self, from: data)
    }
    public func resize(columns: Int, rows: Int) throws {
        guard (1...4096).contains(columns), (1...4096).contains(rows) else { return }
        if trames_session_resize(try currentHandle(), UInt16(rows), UInt16(columns)) < 0 { throw Self.lastError() }
    }
    public func pause(_ value: Bool) throws {
        if trames_session_pause(try currentHandle(), value ? 1 : 0) < 0 { throw Self.lastError() }
    }
    /// Blocking transfer; caller coordinates the selected URL on a background queue.
    public func transfer(guest: String, host: URL, export: Bool, maximumBytes: UInt64? = nil) throws {
        let h = try currentHandle()
        var options: [String:Any] = ["guest":guest, "host":host.path, "export":export]
        if let maximumBytes { options["maximum_bytes"] = maximumBytes }
        let data = try JSONSerialization.data(withJSONObject: options)
        guard data.count <= 65536 else { throw TramesError.message("Transfer request too large") }
        let result = data.withUnsafeBytes { trames_session_transfer(h, $0.bindMemory(to: UInt8.self).baseAddress, UInt32($0.count)) }
        if result < 0 { throw Self.lastError() }
    }
    public func transferAsync(guest: String, host: URL, export: Bool, maximumBytes: UInt64? = nil, authorization: RevocableAccess? = nil) async throws {
        guard guest.utf8.count <= 4096, host.path.utf8.count <= 4096 else { throw TramesError.message("Transfer path exceeds 4 KiB") }
        let h = try currentHandle()
        let token = trames_session_transfer_begin(h)
        guard token != 0 else { throw Self.lastError() }
        let registration = authorization?.register { _ = trames_session_transfer_cancel(h, token) }
        defer { if let registration { authorization?.unregister(registration) } }
        var options: [String:Any] = ["guest":guest, "host":host.path, "export":export, "token":token]
        if let maximumBytes { options["maximum_bytes"] = maximumBytes }
        let data = try JSONSerialization.data(withJSONObject: options)
        try await withTaskCancellationHandler(operation: {
            try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void,Error>) in
                DispatchQueue.global(qos: .userInitiated).async {
                    let result = data.withUnsafeBytes { trames_session_transfer(h, $0.bindMemory(to: UInt8.self).baseAddress, UInt32($0.count)) }
                    if result < 0 { continuation.resume(throwing: Self.lastError()) }
                    else { continuation.resume() }
                }
            }
        }, onCancel: { _ = trames_session_transfer_cancel(h, token) })
    }
    static func lastError() -> Error {
        var bytes = [UInt8](repeating: 0, count: 8192)
        let count = trames_last_error(&bytes, UInt32(bytes.count))
        return TramesError.message(count > 0 ? String(decoding: bytes.prefix(Int(count)), as: UTF8.self) : "Local runtime error")
    }
}
