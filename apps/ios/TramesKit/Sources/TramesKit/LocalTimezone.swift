// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation

/// Original system TZif rules, rather than a fixed current UTC offset.
public struct LocalTimezone: Sendable {
    public let identifier: String
    public let data: Data
    public init(_ zone: TimeZone = .current) throws {
        identifier = zone.identifier
        data = (zone as NSTimeZone).data
        guard (44...16384).contains(data.count), data.starts(with: Data("TZif".utf8)) else {
            throw TramesError.message("Cannot read system timezone rules")
        }
    }
    var options: [String: Any] { ["identifier": identifier, "data": Array(data)] }
}
