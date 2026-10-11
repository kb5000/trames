// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
import CTrames
public indirect enum BridgeValue: Codable, Sendable, Equatable {
    case string(String), number(Double), bool(Bool), object([String:BridgeValue]), array([BridgeValue]), null
    public init(from decoder: Decoder) throws {
        let c = try decoder.singleValueContainer()
        if c.decodeNil() { self = .null }
        else if let v = try? c.decode(Bool.self) { self = .bool(v) }
        else if let v = try? c.decode(String.self) { self = .string(v) }
        else if let v = try? c.decode(Double.self) { self = .number(v) }
        else if let v = try? c.decode([String:BridgeValue].self) { self = .object(v) }
        else { self = .array(try c.decode([BridgeValue].self)) }
    }
    public func encode(to encoder: Encoder) throws {
        var c = encoder.singleValueContainer()
        switch self {
        case .string(let v): try c.encode(v)
        case .number(let v): try c.encode(v)
        case .bool(let v): try c.encode(v)
        case .object(let v): try c.encode(v)
        case .array(let v): try c.encode(v)
        case .null: try c.encodeNil()
        }
    }
    public var text: String? { if case .string(let v) = self { return v }; return nil }
}
public struct BridgeRequest: Decodable, Sendable, Identifiable {
    public let version: UInt32
    public let id: UInt64
    public let method: String
    public let params: [String:BridgeValue]
    public func string(_ key: String, maximum: Int = 4096) throws -> String {
        guard let value = params[key]?.text, value.utf8.count <= maximum, !value.contains("\0") else {
            throw BridgeFailure(code: "invalid_params", message: "Missing or invalid \(key)")
        }
        return value
    }
    public func only(_ names: Set<String>) throws {
        guard Set(params.keys).isSubset(of: names) else { throw BridgeFailure(code: "invalid_params", message: "Unknown parameter") }
    }
}
public struct BridgeHostRequest: Decodable, Sendable, Identifiable {
    public let ticket: UInt64
    public let request: BridgeRequest
    public var id: UInt64 { ticket }
}
public struct BridgeFailure: Error, Codable, Sendable, LocalizedError {
    public let code: String
    public let message: String
    public init(code: String, message: String) { self.code = code; self.message = String(message.prefix(256)) }
    public var errorDescription: String? { message }
}
struct BridgeReply: Encodable {
    let version: UInt32 = 1
    let ticket: UInt64
    let result: BridgeValue?
    let error: BridgeFailure?
}
public enum BridgeValidation {
    public static func webURL(_ value: String) throws -> URL {
        guard value.utf8.count <= 4096, let c = URLComponents(string: value),
            ["http","https"].contains(c.scheme?.lowercased() ?? ""),
            let host = c.host, !host.isEmpty, c.user == nil, c.password == nil, let url = c.url else {
            throw BridgeFailure(code: "invalid_params", message: "Only HTTP and HTTPS links without credentials are allowed")
        }
        return url
    }
    /// Provider paths are relative, contain no traversal and cannot follow links.
    public static func scopedPath(root: URL, relative: String) throws -> URL {
        let parts = relative.split(separator: "/", omittingEmptySubsequences: false)
        guard !relative.isEmpty, relative.utf8.count < 4096, !relative.contains("\0"),
            !parts.contains(where: { $0.isEmpty || $0 == "." || $0 == ".." || $0.utf8.count > 255 }) else {
            throw BridgeFailure(code: "invalid_params", message: "Expected a relative path without traversal")
        }
        var url = root.resolvingSymlinksInPath().standardizedFileURL
        let prefix = url.path.hasSuffix("/") ? url.path : url.path + "/"
        for part in parts {
            url.appendPathComponent(String(part))
            if let attributes = try? FileManager.default.attributesOfItem(atPath: url.path), attributes[.type] as? FileAttributeType == .typeSymbolicLink {
                throw BridgeFailure(code: "denied", message: "Files paths cannot follow symbolic links")
            }
        }
        guard url.resolvingSymlinksInPath().standardizedFileURL.path.hasPrefix(prefix) else {
            throw BridgeFailure(code: "denied", message: "Path escaped the authorized folder")
        }
        return url
    }
}
extension LocalSession {
    public func pollBridge() throws -> BridgeHostRequest? {
        let h = try currentHandle()
        var data = Data(count: 65536)
        let count = data.withUnsafeMutableBytes { trames_session_bridge_poll(h, $0.bindMemory(to: UInt8.self).baseAddress, UInt32($0.count)) }
        guard count >= 0 else { throw Self.lastError() }
        if count == 0 { return nil }
        data.count = Int(count)
        let request = try JSONDecoder().decode(BridgeHostRequest.self, from: data)
        guard request.request.version == 1 else { throw BridgeFailure(code: "invalid_request", message: "Unsupported Bridge version") }
        return request
    }
    public func bridgeActive(_ ticket: UInt64) -> Bool {
        guard let h = try? currentHandle() else { return false }
        return trames_session_bridge_active(h, ticket) == 1
    }
    public func replyBridge(_ ticket: UInt64, result: BridgeValue) throws {
        try bridgeReply(BridgeReply(ticket: ticket, result: result, error: nil))
    }
    public func rejectBridge(_ ticket: UInt64, error: BridgeFailure) throws {
        try bridgeReply(BridgeReply(ticket: ticket, result: nil, error: error))
    }
    private func bridgeReply(_ reply: BridgeReply) throws {
        let h = try currentHandle()
        let data = try JSONEncoder().encode(reply)
        guard data.count <= 32768 else { throw BridgeFailure(code: "too_large", message: "Native response exceeds 32 KiB") }
        let result = data.withUnsafeBytes { trames_session_bridge_reply(h, $0.bindMemory(to: UInt8.self).baseAddress, UInt32($0.count)) }
        if result < 0 { throw Self.lastError() }
    }
}
