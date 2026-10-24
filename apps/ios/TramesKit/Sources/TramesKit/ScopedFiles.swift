// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
#if canImport(Darwin)
import Darwin
/// Coordination is limited to a synchronous bounded copy. Provider URLs never
/// become guest paths, and no security-scoped permission outlives the copy.
public enum ScopedFiles {
    public static func authorizedCopy(source: URL, destination: URL, export: Bool, maximumBytes: UInt64,
                                      authorization: RevocableAccess, validate: @escaping @Sendable (URL) throws -> Void) async throws {
        let cancellation = CopyCancellation()
        try await withTaskCancellationHandler(operation: {
            try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void,Error>) in
                DispatchQueue.global(qos: .userInitiated).async {
                    do {
                        var coordinationError: NSError?, copyError: Error?
                        let accessor: (URL) -> Void = { coordinated in
                            do {
                                try authorization.check(); try cancellation.check(); try validate(coordinated)
                                try copy(source: export ? source : coordinated, destination: export ? coordinated : destination,
                                         cancellation: cancellation, authorization: authorization, maximumBytes: maximumBytes)
                            } catch { copyError = error }
                        }
                        if export {
                            NSFileCoordinator().coordinate(writingItemAt: destination, options: .forReplacing, error: &coordinationError, byAccessor: accessor)
                        } else {
                            NSFileCoordinator().coordinate(readingItemAt: source, options: .withoutChanges, error: &coordinationError, byAccessor: accessor)
                        }
                        if let error = copyError ?? coordinationError { throw error }
                        continuation.resume()
                    } catch { continuation.resume(throwing: error) }
                }
            }
        }, onCancel: { cancellation.cancel() })
    }
    public static func stage(selected: URL, destination: URL) async throws {
        let cancellation = CopyCancellation()
        try await withTaskCancellationHandler(operation: {
            try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void,Error>) in
                DispatchQueue.global(qos: .userInitiated).async {
                    do {
                        let scoped = selected.startAccessingSecurityScopedResource()
                        defer { if scoped { selected.stopAccessingSecurityScopedResource() } }
                        var coordinationError: NSError?, copyError: Error?
                        NSFileCoordinator().coordinate(readingItemAt: selected, options: .withoutChanges, error: &coordinationError) { source in
                            do { try copy(source: source, destination: destination, cancellation: cancellation) }
                            catch { copyError = error }
                        }
                        if let error = copyError ?? coordinationError { throw error }
                        continuation.resume()
                    } catch { continuation.resume(throwing: error) }
                }
            }
        }, onCancel: { cancellation.cancel() })
    }
    private static func copy(source: URL, destination: URL, cancellation: CopyCancellation, authorization: RevocableAccess? = nil, maximumBytes: UInt64 = UInt64.max) throws {
        let metadata = try source.resourceValues(forKeys: [.isRegularFileKey])
        guard metadata.isRegularFile == true else { throw TramesError.message("Choose a regular file") }
        let input = try FileHandle(forReadingFrom: source)
        defer { try? input.close() }
        let fd = destination.path.withCString { Darwin.open($0, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0o600) }
        guard fd >= 0 else { throw TramesError.message("Cannot create destination: \(String(cString: strerror(errno)))") }
        let output = FileHandle(fileDescriptor: fd, closeOnDealloc: true)
        defer { try? output.close() }
        do {
            var count: UInt64 = 0
            while true {
                let copied=try autoreleasepool {
                    try cancellation.check(); try authorization?.check()
                    let data=try input.read(upToCount:65536) ?? Data()
                    guard UInt64(data.count)<=maximumBytes-count else {throw BridgeFailure(code:"too_large",message:"Files transfer exceeds its size limit")}
                    try output.write(contentsOf:data);return data.count
                }
                if copied==0 {break}
                count+=UInt64(copied)
            }
            try cancellation.check(); try authorization?.check()
        } catch { try? FileManager.default.removeItem(at: destination); throw error }
    }
}
private final class CopyCancellation: @unchecked Sendable {
    private let lock = NSLock()
    private var cancelled = false
    func cancel() { lock.lock(); cancelled = true; lock.unlock() }
    func check() throws { lock.lock(); defer { lock.unlock() }; if cancelled { throw CancellationError() } }
}
#endif
