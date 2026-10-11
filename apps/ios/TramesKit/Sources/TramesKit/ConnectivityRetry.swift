// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif
/// Retry only repeatable bootstrap reads, never installation or native effects.
public enum ConnectivityRetry {
    public static func perform<T>(
        operation: @Sendable () async throws -> T,
        delay: @Sendable (UInt64) async throws -> Void = { try await Task.sleep(nanoseconds:$0) }
    ) async throws -> T {
        for attempt in 0..<3 {
            try Task.checkCancellation()
            do { return try await operation() }
            catch {
                try Task.checkCancellation()
                guard attempt < 2, let error=error as? URLError,
                      [.notConnectedToInternet,.networkConnectionLost,.cannotFindHost,
                       .cannotConnectToHost,.dnsLookupFailed,.timedOut,.dataNotAllowed].contains(error.code)
                else {throw error}
                try await delay(UInt64(attempt+1)*500_000_000)
            }
        }
        throw URLError(.notConnectedToInternet)
    }
    public static func download(from url: URL) async throws -> (URL,URLResponse) {
        let configuration=URLSessionConfiguration.ephemeral
        #if canImport(Darwin)
        configuration.waitsForConnectivity=true
        #endif
        configuration.timeoutIntervalForRequest=30
        configuration.timeoutIntervalForResource=60
        let session=URLSession(configuration:configuration)
        defer {session.invalidateAndCancel()}
        return try await perform {try await session.download(from:url)}
    }
}
