// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
import TramesKit
actor EnvironmentStore {
    static let archiveURL = URL(string: "https://dl-cdn.alpinelinux.org/alpine/v3.22/releases/aarch64/alpine-minirootfs-3.22.6-aarch64.tar.gz")!
    let root: URL
    init() throws {
        root = try FileManager.default.url(for: .applicationSupportDirectory, in: .userDomainMask, appropriateFor: nil, create: true).appendingPathComponent("Trames", isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
    }
    func start(useVolumeBudget: Bool) async throws -> LocalSession {
        let active = root.appendingPathComponent("environment", isDirectory: true)
        let values = try root.resourceValues(forKeys: [.volumeTotalCapacityKey, .volumeAvailableCapacityForImportantUsageKey])
        guard let total = values.volumeTotalCapacity, let available = values.volumeAvailableCapacityForImportantUsage, total > 0, available >= 0 else {
            throw TramesError.message("Cannot determine device storage capacity")
        }
        if !FileManager.default.fileExists(atPath: active.path) {
            let (download, response) = try await URLSession.shared.download(from: Self.archiveURL)
            defer { try? FileManager.default.removeItem(at: download) }
            guard (response as? HTTPURLResponse)?.statusCode == 200,
                (try download.resourceValues(forKeys: [.fileSizeKey])).fileSize ?? Int.max <= 16*1024*1024 else { throw TramesError.message("Invalid Alpine rootfs download") }
            let staging = root.appendingPathComponent("install-\(UUID().uuidString)", isDirectory: true)
            do {
                try await Task.detached(priority: .userInitiated) {
                    try LocalSession.install(store: staging, rootfs: download, total: UInt64(total), available: UInt64(available))
                }.value
                try Task.checkCancellation()
                try FileManager.default.moveItem(at: staging, to: active)
            } catch { try? FileManager.default.removeItem(at: staging); throw error }
        }
        return try LocalSession(store: active, rootfs: nil, total: UInt64(total), available: UInt64(available), useVolumeBudget: useVolumeBudget)
    }
    func knownHosts() throws -> KnownHosts { try KnownHosts(url: root.appendingPathComponent("known-hosts.json")) }
}
