// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
import XCTest
@testable import Trames

final class RootfsProviderPathTests:XCTestCase {
    func testUnmaterializedURLKeepsItsIdentityWhenStorageAlreadyExists() throws {
        let temporary=FileManager.default.temporaryDirectory
        let alias=URL(fileURLWithPath:"/private"+temporary.path,isDirectory:true)
        let parent=FileManager.default.fileExists(atPath:alias.path) ? alias : temporary
        let storage=parent.appendingPathComponent("provider-\(UUID().uuidString)",isDirectory:true)
        try FileManager.default.createDirectory(at:storage,withIntermediateDirectories:true)
        defer {try? FileManager.default.removeItem(at:storage)}
        let file=storage.appendingPathComponent("encoded-id",isDirectory:true).appendingPathComponent("new file.txt")
        XCTAssertEqual(RootfsProviderPath.relativeComponents(of:file,in:storage),["encoded-id","new file.txt"])
        try FileManager.default.createDirectory(at:file.deletingLastPathComponent(),withIntermediateDirectories:true)
        try Data("provider content".utf8).write(to:file)
        XCTAssertEqual(RootfsProviderPath.relativeComponents(of:file,in:storage),["encoded-id","new file.txt"])
        XCTAssertEqual(RootfsProviderPath.relativeComponents(of:storage,in:storage),[])
    }
    func testRejectsSiblingTraversalAndForeignURLs() {
        let storage=URL(fileURLWithPath:"/private/tmp/provider",isDirectory:true)
        for value in ["file:///private/tmp/provider-other/id/file.txt",
                      "file:///private/tmp/provider/../outside/id/file.txt",
                      "https://example.com/private/tmp/provider/id/file.txt",
                      "file://other-host/private/tmp/provider/id/file.txt"] {
            XCTAssertNil(RootfsProviderPath.relativeComponents(of:URL(string:value)!,in:storage),value)
        }
    }
    func testCanonicalComparisonRequiresTheSameFilenameAndDirectory() {
        let expected=URL(fileURLWithPath:"/private/tmp/provider/encoded-id/文档.txt")
        XCTAssertTrue(RootfsProviderPath.sameLocation(expected,URL(string:expected.absoluteString)!))
        XCTAssertFalse(RootfsProviderPath.sameLocation(expected,expected.deletingLastPathComponent().appendingPathComponent("other.txt")))
        XCTAssertFalse(RootfsProviderPath.sameLocation(expected,URL(fileURLWithPath:"/private/tmp/provider-other/encoded-id/文档.txt")))
    }
}
