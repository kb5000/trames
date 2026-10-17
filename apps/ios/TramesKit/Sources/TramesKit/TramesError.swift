// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
public enum TramesError: Error, LocalizedError {
    case message(String)
    public var errorDescription: String? { switch self { case .message(let text): return text } }
}
