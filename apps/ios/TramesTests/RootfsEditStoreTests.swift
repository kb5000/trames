// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
import Darwin
import XCTest
@testable import Trames

final class RootfsEditStoreTests:XCTestCase {
    private func fixture() throws -> (URL,RootfsEditStore,RootfsEditBase,URL) {
        let root=FileManager.default.temporaryDirectory.appendingPathComponent("edits-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at:root,withIntermediateDirectories:true)
        let source=root.appendingPathComponent("editor.txt")
        try Data("original".utf8).write(to:source)
        let copied=try RootfsEditStore.copy(source,to:root.appendingPathComponent("copy"))
        let base=RootfsEditBase(identifier:"node/alpine/p/1234",environment:"alpine",path:"/root/editor.txt",version:.init(inode:4,sha256:copied.sha256))
        let store=RootfsEditStore(root:root);try store.record(base)
        return (root,store,base,source)
    }
    func testRepeatedSavesCoalesceAndPersistUntilExplicitReplay() throws {
        let (root,store,base,source)=try fixture();defer {try? FileManager.default.removeItem(at:root)}
        try Data("draft".utf8).write(to:source);try store.stage(base.identifier,from:source)
        let first=try XCTUnwrap(store.request(base.identifier))
        try Data("final edited document".utf8).write(to:source);try store.stage(base.identifier,from:source)
        let reopened=RootfsEditStore(root:root),last=try XCTUnwrap(reopened.request(base.identifier))
        XCTAssertNotEqual(last.token,first.token)
        XCTAssertEqual(last.base.version,base.version)
        XCTAssertFalse(FileManager.default.fileExists(atPath:store.payload(first).path))
        XCTAssertEqual(try Data(contentsOf:store.payload(last)),Data("final edited document".utf8))
        var committed=0
        XCTAssertTrue(reopened.replay { request,payload in
            committed+=1;XCTAssertEqual(request.token,last.token)
            XCTAssertEqual(try Data(contentsOf:payload),Data("final edited document".utf8))
            return .init(inode:8,sha256:request.sha256)
        }.isEmpty)
        XCTAssertEqual(committed,1);XCTAssertNil(store.request(base.identifier))
        XCTAssertEqual(store.base(base.identifier)?.version,.init(inode:8,sha256:last.sha256))
    }
    func testConflictPreservesSavedBytesAndOriginalComparisonVersion() throws {
        let (root,store,base,source)=try fixture();defer {try? FileManager.default.removeItem(at:root)}
        try Data("keep this edit".utf8).write(to:source);try store.stage(base.identifier,from:source)
        let failures=store.replay { _,_ in throw NSError(domain:"conflict",code:1) }
        XCTAssertEqual(failures.count,1)
        let pending=try XCTUnwrap(store.request(base.identifier));XCTAssertNotNil(pending.error)
        XCTAssertEqual(pending.base.version,base.version)
        var unrelated=base;unrelated.version = .init(inode:99,sha256:"different")
        try store.record(unrelated)
        XCTAssertEqual(store.base(base.identifier)?.version,base.version)
        let destination=root.appendingPathComponent("reopened.txt")
        XCTAssertTrue(try store.materializePending(base.identifier,to:destination))
        XCTAssertEqual(try Data(contentsOf:destination),Data("keep this edit".utf8))
    }
    func testRevertingToOpenedContentClearsPendingWithoutACommit() throws {
        let (root,store,base,source)=try fixture();defer {try? FileManager.default.removeItem(at:root)}
        try Data("edited".utf8).write(to:source);try store.stage(base.identifier,from:source)
        XCTAssertEqual(store.requests().count,1)
        try Data("original".utf8).write(to:source);try store.stage(base.identifier,from:source)
        XCTAssertTrue(store.requests().isEmpty)
        XCTAssertTrue(store.replay {_,_ in XCTFail("Unchanged bytes must not be committed");return base.version}.isEmpty)
        XCTAssertEqual(store.base(base.identifier)?.version,base.version)
    }
    func testMaterializationCannotOverwriteAnUnstagedOrRevertedEdit() throws {
        let (root,store,base,source)=try fixture();defer {try? FileManager.default.removeItem(at:root)}
        XCTAssertFalse(try store.preservesLocalEdit(base.identifier,at:source))
        try Data("new bytes before itemChanged".utf8).write(to:source)
        XCTAssertTrue(try store.preservesLocalEdit(base.identifier,at:source))
        try store.stage(base.identifier,from:source)
        try Data("original".utf8).write(to:source)
        // Even reverting a staged edit must reach itemChanged before a read
        // can replace the editor's cache with the previous pending payload.
        XCTAssertTrue(try store.preservesLocalEdit(base.identifier,at:source))
        try store.stage(base.identifier,from:source)
        XCTAssertFalse(try store.preservesLocalEdit(base.identifier,at:source))
    }
    func testForegroundReplayAcceptsASaveWhoseRequestIsNotPublishedYet() throws {
        let (root,store,base,source)=try fixture();defer {try? FileManager.default.removeItem(at:root)}
        let locks=try FileManager.default.contentsOfDirectory(at:root.appendingPathComponent("file-provider-locks"),includingPropertiesForKeys:nil)
        let key=try XCTUnwrap(locks.first).deletingPathExtension().lastPathComponent
        let pending=root.appendingPathComponent("file-provider-pending").appendingPathComponent(key)
        try FileManager.default.createDirectory(at:pending,withIntermediateDirectories:true)
        XCTAssertTrue(store.replay {_,_ in XCTFail("No published request");return base.version}.isEmpty)
        try Data("completed after foreground".utf8).write(to:source)
        try store.stage(base.identifier,from:source)
        XCTAssertTrue(store.replay { request,_ in .init(inode:8,sha256:request.sha256)}.isEmpty)
        XCTAssertNil(store.request(base.identifier))
        XCTAssertEqual(store.base(base.identifier)?.version.inode,8)
    }
    func testStreamingCopyHandlesMultipleChunksAndEmptyDocuments() throws {
        let (root,_,_,source)=try fixture();defer {try? FileManager.default.removeItem(at:root)}
        let data=Data(repeating:37,count:3*65536+7);try data.write(to:source)
        let destination=root.appendingPathComponent("large.txt"),result=try RootfsEditStore.copy(source,to:destination)
        XCTAssertEqual(result.size,UInt64(data.count));XCTAssertEqual(result.sha256.count,64)
        XCTAssertEqual(try Data(contentsOf:destination),data)
        try Data().write(to:source)
        let empty=try RootfsEditStore.copy(source,to:destination)
        XCTAssertEqual(empty.size,0);XCTAssertEqual(try Data(contentsOf:destination),Data())
        XCTAssertEqual(empty.sha256,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
    }
    func testProviderIdentifierSurvivesAtomicContentReplacement() {
        let old=RootfsEntry(path:Data("/root/document.txt".utf8),inode:4,kind:1,mode:0o100644,modified:Date())
        let new=RootfsEntry(path:old.path,inode:8,kind:1,mode:old.mode,modified:Date())
        XCTAssertEqual(old.identifier(environment:"alpine"),new.identifier(environment:"alpine"))
        let other=RootfsEntry(path:Data("/root/other.txt".utf8),inode:4,kind:1,mode:old.mode,modified:old.modified)
        XCTAssertNotEqual(old.identifier(environment:"alpine"),other.identifier(environment:"alpine"))
    }
    func testLargeSaveHasBoundedMemoryAndRetainsFailedReplay() throws {
        guard let count=Int(ProcessInfo.processInfo.environment["TRAMES_FILEPROVIDER_LARGE_MIB"] ?? ""),count>=128,count<=4096 else {
            throw XCTSkip("Set TRAMES_FILEPROVIDER_LARGE_MIB=256 for the disk/memory probe")
        }
        let (root,store,base,source)=try fixture();defer {try? FileManager.default.removeItem(at:root)}
        let output=try FileHandle(forWritingTo:source)
        try output.truncate(atOffset:0)
        let chunk=Data(repeating:97,count:1024*1024)
        for _ in 0..<count {try output.write(contentsOf:chunk)}
        try output.close()
        var before=rusage(),after=rusage();getrusage(RUSAGE_SELF,&before)
        let began=Date();try store.stage(base.identifier,from:source)
        let request=try XCTUnwrap(store.request(base.identifier))
        XCTAssertEqual(request.size,UInt64(count)*1024*1024)
        XCTAssertEqual(try RootfsEditStore.checksum(store.payload(request)),request.sha256)
        XCTAssertEqual(store.replay {_,_ in throw NSError(domain:"quota",code:28)}.count,1)
        XCTAssertNotNil(store.request(base.identifier)?.error)
        XCTAssertEqual(try FileManager.default.attributesOfItem(atPath:store.payload(request).path)[.size] as? UInt64,request.size)
        getrusage(RUSAGE_SELF,&after)
        let growth=max(0,after.ru_maxrss-before.ru_maxrss)
        print("Large FileProvider journal: \(count) MiB, \(Date().timeIntervalSince(began)) s, peak RSS growth \(Double(growth)/1048576) MiB")
        XCTAssertLessThan(growth,64*1024*1024,"Saving must not retain a whole large document in memory")
    }
}
