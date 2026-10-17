// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
import TramesKit
public struct SSHHost: Equatable, Sendable {
    public var alias: String
    public var hostname: String
    public var port: Int
    public var user: String
    public var identityFile: String?
    public var warnings: [String]
    public var endpoint: String { "[\(hostname.lowercased())]:\(port)" }
}
public struct SSHConfig: Sendable {
    private struct Block: Sendable { var patterns: [String]; var values: [(String,String)] }
    private var blocks: [Block] = []
    public private(set) var aliases: [String] = []
    public init(_ text: String) throws {
        guard text.utf8.count <= 65536 else { throw TramesError.message("SSH config exceeds 64 KiB") }
        blocks = [Block(patterns: ["*"], values: [])]
        for (index,line) in text.components(separatedBy: .newlines).enumerated() {
            let tokens = try Self.tokens(line)
            guard !tokens.isEmpty else { continue }
            let directive = tokens[0].lowercased(), values = Array(tokens.dropFirst())
            guard !values.isEmpty else { throw TramesError.message("SSH config line \(index+1): value required") }
            if directive == "host" {
                blocks.append(Block(patterns: values, values: []))
                for alias in values where !alias.contains("*") && !alias.contains("?") && !alias.hasPrefix("!") {
                    if !aliases.contains(alias) { aliases.append(alias) }
                }
            } else if directive == "match" {
                throw TramesError.message("SSH config Match blocks are unsupported")
            } else { blocks[blocks.count-1].values.append((directive,values.joined(separator: " "))) }
        }
    }
    public func resolve(_ alias: String, defaultUser: String = "user") throws -> SSHHost {
        guard !alias.isEmpty && alias.utf8.count <= 255 && !alias.contains(where: { $0.isWhitespace || $0.asciiValue == 0 }) else { throw TramesError.message("Invalid SSH host alias") }
        var found: [String:String] = [:], warnings: [String] = []
        let supported: Set<String> = ["hostname", "port", "user", "identityfile"]
        for block in blocks where Self.matches(block.patterns,alias) {
            for (key,value) in block.values {
                if supported.contains(key) { if found[key] == nil { found[key] = value } }
                else if !warnings.contains(key) { warnings.append(key) }
            }
        }
        let port = Int(found["port"] ?? "22") ?? 0
        let host = (found["hostname"] ?? alias).replacingOccurrences(of: "%h", with: alias)
        let user = found["user"] ?? defaultUser
        guard (1...65535).contains(port), !host.isEmpty, host.utf8.count <= 255,
            !host.contains(where: { $0.isWhitespace || $0 == "/" || $0.asciiValue == 0 }),
            !user.isEmpty, user.utf8.count <= 255, !user.contains(where: { $0.asciiValue == 0 }) else { throw TramesError.message("Invalid SSH hostname, port or user") }
        if let identity = found["identityfile"], identity.contains("%") { throw TramesError.message("IdentityFile tokens are unsupported") }
        return SSHHost(alias: alias, hostname: host, port: port, user: user, identityFile: found["identityfile"], warnings: warnings.map { "Unsupported native SSH directive: \($0)" })
    }
    private static func matches(_ patterns: [String], _ value: String) -> Bool {
        var positive = false
        for pattern in patterns {
            let negative = pattern.hasPrefix("!"), raw = pattern.hasPrefix("!") ? String(pattern.dropFirst()) : pattern
            let expression = "^" + raw.map { c in c == "*" ? ".*" : c == "?" ? "." : NSRegularExpression.escapedPattern(for: String(c)) }.joined() + "$"
            if value.range(of: expression, options: [.regularExpression, .caseInsensitive]) != nil {
                if negative { return false }; positive = true
            }
        }
        return positive
    }
    private static func tokens(_ line: String) throws -> [String] {
        var tokens: [String] = [], token = "", quote: Character?, escaped = false, separatorConsumed = false
        for c in line {
            if escaped { token.append(c); escaped = false; continue }
            if c == "\\" { escaped = true; continue }
            if let q = quote { if c == q { quote = nil } else { token.append(c) } }
            else if c == "\"" || c == "'" { quote = c }
            else if c == "#" { break }
            else if c == "=" && !separatorConsumed && (tokens.isEmpty || (tokens.count == 1 && token.isEmpty)) {
                if !token.isEmpty { tokens.append(token); token = "" }
                separatorConsumed = true
            } else if c.isWhitespace {
                if !token.isEmpty { tokens.append(token); token = "" }
            } else { token.append(c) }
        }
        guard quote == nil && !escaped else { throw TramesError.message("Unterminated SSH config quote or escape") }
        if !token.isEmpty { tokens.append(token) }; return tokens
    }
}
