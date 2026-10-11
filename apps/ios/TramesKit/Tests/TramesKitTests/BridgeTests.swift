// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
@testable import TramesKit
final class BridgeTests: XCTestCase {
    func testTicketPrecisionAndBoundedParameterValidation() throws {
        let text = "{\"ticket\":18446744073709551615,\"request\":{\"version\":1,\"id\":18446744073709551615,\"method\":\"clipboard.write\",\"params\":{\"text\":\"你好\\n\",\"extra\":true}}}"
        let host = try JSONDecoder().decode(BridgeHostRequest.self, from: Data(text.utf8))
        XCTAssertEqual(host.ticket,UInt64.max); XCTAssertEqual(host.request.id,UInt64.max)
        XCTAssertEqual(try host.request.string("text"),"你好\n")
        XCTAssertThrowsError(try host.request.only(["text"]))
        XCTAssertThrowsError(try host.request.string("text", maximum: 2))
        XCTAssertThrowsError(try host.request.string("missing"))
    }
    func testOnlyWebLinksWithoutCredentialsAreAccepted() throws {
        XCTAssertEqual(try BridgeValidation.webURL("https://example.com/a?q=1").host,"example.com")
        for text in ["file:///private/file", "javascript:alert(1)", "trames://x", "https://user:password@example.com", "https:///", "http://"] {
            XCTAssertThrowsError(try BridgeValidation.webURL(text),text)
        }
    }
    func testRelativePathsRejectTraversalAndAllSymbolicLinks() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }
        XCTAssertEqual(try BridgeValidation.scopedPath(root: root, relative: "file.txt").lastPathComponent,"file.txt")
        for path in ["", "/etc/passwd", "../outside", "sub/../../outside", "a//b", "./file", "a/", "a\0b"] {
            XCTAssertThrowsError(try BridgeValidation.scopedPath(root: root, relative: path),path)
        }
        try FileManager.default.createSymbolicLink(at: root.appendingPathComponent("alias"), withDestinationURL: root)
        XCTAssertThrowsError(try BridgeValidation.scopedPath(root: root, relative: "alias/file"))
    }
    func testRevocationCancelsRegisteredAndFutureOperations() throws {
        final class Count: @unchecked Sendable {
            var value=0; let lock=NSLock()
            func increment() { lock.lock(); value+=1; lock.unlock() }
        }
        let count=Count(), access=RevocableAccess()
        let token=access.register { count.increment() }
        try access.check(); access.revoke(); access.revoke(); access.unregister(token)
        XCTAssertEqual(count.value,1); XCTAssertThrowsError(try access.check())
        _ = access.register { count.increment() }; XCTAssertEqual(count.value,2)
    }
    #if canImport(Darwin)
    func testCoordinatedCopiesAreExclusiveBoundedAndRevocable() async throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }
        let source=root.appendingPathComponent("source"), destination=root.appendingPathComponent("copy")
        let data=Data(repeating:42,count:131072); try data.write(to:source)
        let access=RevocableAccess()
        try await ScopedFiles.authorizedCopy(source: source,destination: destination,export: false,maximumBytes: 131072,authorization: access,validate:{_ in})
        XCTAssertEqual(try Data(contentsOf: destination),data)
        do { try await ScopedFiles.authorizedCopy(source: source,destination: destination,export: true,maximumBytes: 131072,authorization: access,validate:{_ in}); XCTFail("Existing file was replaced") } catch {}
        XCTAssertEqual(try Data(contentsOf: destination),data)
        let limited=root.appendingPathComponent("limited")
        do { try await ScopedFiles.authorizedCopy(source: source,destination: limited,export: false,maximumBytes: 65536,authorization: access,validate:{_ in}); XCTFail("Limit ignored") } catch {}
        XCTAssertFalse(FileManager.default.fileExists(atPath:limited.path))
        access.revoke()
        do { try await ScopedFiles.authorizedCopy(source: source,destination: limited,export: true,maximumBytes: 131072,authorization: access,validate:{_ in}); XCTFail("Revocation ignored") } catch {}
        XCTAssertFalse(FileManager.default.fileExists(atPath:limited.path))
    }
    func testFolderBookmarksPersistAndRevocationSurvivesReopen() async throws {
        let root=FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at:root,withIntermediateDirectories:true)
        defer {try? FileManager.default.removeItem(at:root)}
        let file=root.appendingPathComponent("grants.json")
        let store=try ScopedDirectories(file:file); try await store.add(selected:root,writable:false)
        let grants=await store.list(); XCTAssertEqual(grants.count,1); XCTAssertFalse(grants[0].writable)
        let reopened=try ScopedDirectories(file:file); let restored=await reopened.list(); XCTAssertEqual(restored[0].id,grants[0].id)
        try await store.revoke(grants[0].id)
        let empty=try ScopedDirectories(file:file); let result=await empty.list(); XCTAssertTrue(result.isEmpty)
    }
    #endif
}
