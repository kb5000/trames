// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
#if canImport(Darwin)
/// Coordination is limited to a synchronous bounded copy. Provider URLs never
/// become guest paths, and no security-scoped permission outlives the copy.
public enum ScopedFiles {
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
    private static func copy(source: URL, destination: URL, cancellation: CopyCancellation) throws {
        let metadata = try source.resourceValues(forKeys: [.isRegularFileKey])
        guard metadata.isRegularFile == true else { throw TramesError.message("Choose a regular file") }
        let input = try FileHandle(forReadingFrom: source)
        defer { try? input.close() }
        guard !FileManager.default.fileExists(atPath: destination.path), FileManager.default.createFile(atPath: destination.path, contents: nil) else { throw TramesError.message("Staging destination already exists") }
        let output = try FileHandle(forWritingTo: destination)
        defer { try? output.close() }
        do {
            while let data = try input.read(upToCount: 65536), !data.isEmpty {
                try cancellation.check(); try output.write(contentsOf: data)
            }
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
