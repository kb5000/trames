// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
#if canImport(Darwin)
public struct FolderGrant: Codable, Identifiable, Sendable {
    public let id: UUID
    public let name: String
    public let writable: Bool
    fileprivate var bookmark: Data
}
/// Persistent bookmarks remain native. The guest sees only opaque grant IDs.
public actor ScopedDirectories {
    private let file: URL
    private var grants: [FolderGrant]
    private var access: [UUID:RevocableAccess] = [:]
    public static let maximumBytes: UInt64 = 1 << 30
    public init(file: URL) throws {
        self.file = file
        if FileManager.default.fileExists(atPath: file.path) {
            let size = try file.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? Int.max
            guard size <= 512*1024 else { throw BridgeFailure(code: "invalid_grants", message: "Folder authorization file is too large") }
            let data = try Data(contentsOf: file)
            guard data.count <= 512*1024 else { throw BridgeFailure(code: "invalid_grants", message: "Folder authorization file is too large") }
            grants = try JSONDecoder().decode([FolderGrant].self, from: data)
            guard grants.count <= 32, Set(grants.map(\.id)).count == grants.count else { throw BridgeFailure(code: "invalid_grants", message: "Invalid folder authorization list") }
        } else { grants = [] }
        for grant in grants { access[grant.id] = RevocableAccess() }
    }
    public func list() -> [FolderGrant] { grants }
    private func persist(_ values: [FolderGrant]) throws {
        let data = try JSONEncoder().encode(values)
        guard data.count <= 512*1024 else { throw BridgeFailure(code: "too_large", message: "Folder bookmarks exceed their budget") }
        try data.write(to: file, options: .atomic)
        try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: file.path)
    }
    public func add(selected: URL, writable: Bool) throws {
        guard grants.count < 32 else { throw BridgeFailure(code: "busy", message: "At most 32 folders may be authorized") }
        let scoped = selected.startAccessingSecurityScopedResource()
        defer { if scoped { selected.stopAccessingSecurityScopedResource() } }
        guard try selected.resourceValues(forKeys: [.isDirectoryKey]).isDirectory == true else { throw BridgeFailure(code: "invalid_params", message: "Choose a folder") }
        #if os(macOS)
        let bookmark = try selected.bookmarkData(options: .withSecurityScope, includingResourceValuesForKeys: nil, relativeTo: nil)
        #else
        let bookmark = try selected.bookmarkData(options: .minimalBookmark, includingResourceValuesForKeys: nil, relativeTo: nil)
        #endif
        let grant = FolderGrant(id: UUID(), name: String(selected.lastPathComponent.prefix(128)), writable: writable, bookmark: bookmark)
        try persist(grants + [grant]); grants.append(grant); access[grant.id] = RevocableAccess()
    }
    public func revoke(_ id: UUID) throws {
        let values = grants.filter { $0.id != id }
        try persist(values); access.removeValue(forKey: id)?.revoke(); grants = values
    }
    public func transfer(id: UUID, relative: String, guest: String, export: Bool, session: LocalSession) async throws {
        guard let index = grants.firstIndex(where: { $0.id == id }), let authorization = access[id] else {
            throw BridgeFailure(code: "revoked", message: "Unknown or revoked folder authorization")
        }
        let grant = grants[index]
        guard !export || grant.writable else { throw BridgeFailure(code: "denied", message: "This folder allows import only") }
        guard guest.hasPrefix("/"), guest.utf8.count < 4096, !guest.contains("\0") else { throw BridgeFailure(code: "invalid_params", message: "Expected an absolute Linux file path") }
        var stale = false
        #if os(macOS)
        let folder = try URL(resolvingBookmarkData: grant.bookmark, options: [.withSecurityScope, .withoutUI], relativeTo: nil, bookmarkDataIsStale: &stale)
        #else
        let folder = try URL(resolvingBookmarkData: grant.bookmark, options: [.withoutUI], relativeTo: nil, bookmarkDataIsStale: &stale)
        #endif
        let scoped = folder.startAccessingSecurityScopedResource()
        defer { if scoped { folder.stopAccessingSecurityScopedResource() } }
        try authorization.check()
        if stale {
            #if os(macOS)
            let updated = try folder.bookmarkData(options: .withSecurityScope, includingResourceValuesForKeys: nil, relativeTo: nil)
            #else
            let updated = try folder.bookmarkData(options: .minimalBookmark, includingResourceValuesForKeys: nil, relativeTo: nil)
            #endif
            var values = grants; values[index].bookmark = updated; try persist(values); grants = values
        }
        let provider = try BridgeValidation.scopedPath(root: folder, relative: relative)
        let staging = FileManager.default.temporaryDirectory.appendingPathComponent("trames-bridge-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: staging) }
        let validate: @Sendable (URL) throws -> Void = { coordinated in
            try authorization.check()
            let checked = try BridgeValidation.scopedPath(root: folder, relative: relative)
            guard checked.standardizedFileURL == coordinated.standardizedFileURL else {
                throw BridgeFailure(code: "denied", message: "Provider changed the authorized path")
            }
        }
        if export {
            try await session.transferAsync(guest: guest, host: staging, export: true, maximumBytes: Self.maximumBytes, authorization: authorization)
            try await ScopedFiles.authorizedCopy(source: staging, destination: provider, export: true, maximumBytes: Self.maximumBytes, authorization: authorization, validate: validate)
        } else {
            try await ScopedFiles.authorizedCopy(source: provider, destination: staging, export: false, maximumBytes: Self.maximumBytes, authorization: authorization, validate: validate)
            try await session.transferAsync(guest: guest, host: staging, export: false, maximumBytes: Self.maximumBytes, authorization: authorization)
        }
    }
}
#endif
