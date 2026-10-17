// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
#if canImport(Darwin)
import Darwin
#endif

enum RootfsCatalogError: Error { case invalidStore, noSuchItem, unavailable }
struct RootfsEnvironment: Codable, Equatable {
    let id: String
    let name: String
    let store: String
    func storeURL(in root:URL) throws -> URL {
        guard !id.isEmpty,id.utf8.count<=64,id.allSatisfy({$0.isASCII && ($0.isLetter || $0.isNumber || $0=="-")}),
              !name.isEmpty,name.utf8.count<=255,!name.contains("/"),!name.utf8.contains(0),
              !store.hasPrefix("/"),!store.split(separator:"/",omittingEmptySubsequences:false).contains(where:{$0.isEmpty || $0=="." || $0==".."}) else { throw RootfsCatalogError.invalidStore }
        let url=root.appendingPathComponent(store,isDirectory:true).resolvingSymlinksInPath()
        guard url.path.hasPrefix(root.resolvingSymlinksInPath().path+"/") else { throw RootfsCatalogError.invalidStore }
        return url
    }
}
struct RootfsCatalog: Codable {
    let version: Int
    let environments: [RootfsEnvironment]
    static let alpine=RootfsCatalog(version:1,environments:[RootfsEnvironment(id:"alpine",name:"Alpine",store:"environment")])
    static func load(_ root:URL) throws -> RootfsCatalog {
        let data=try Data(contentsOf:root.appendingPathComponent("roots.json"))
        guard data.count<=65536 else { throw RootfsCatalogError.invalidStore }
        let result=try JSONDecoder().decode(Self.self,from:data)
        guard result.version==1,result.environments.count<=64,Set(result.environments.map(\.id)).count==result.environments.count else { throw RootfsCatalogError.invalidStore }
        for environment in result.environments { _=try environment.storeURL(in:root) }
        return result
    }
    func save(_ root:URL) throws {
        try JSONEncoder().encode(self).write(to:root.appendingPathComponent("roots.json"),options:.atomic)
    }
}
enum RootfsLocations {
    static var group: String? { Bundle.main.object(forInfoDictionaryKey:"TramesAppGroup") as? String }
    static var sharedRoot: URL? {
        guard let group else { return nil }
        return FileManager.default.containerURL(forSecurityApplicationGroupIdentifier:group)?.appendingPathComponent("Trames",isDirectory:true)
    }
    static func prepare(privateRoot:URL,sharedRoot:URL?) throws -> URL {
        guard let sharedRoot else { return privateRoot }
        let fm=FileManager.default
        let old=privateRoot.appendingPathComponent("environment",isDirectory:true)
        let shared=sharedRoot.appendingPathComponent("environment",isDirectory:true)
        do {
            try fm.createDirectory(at:sharedRoot,withIntermediateDirectories:true)
            // Never choose a second root silently if both stores contain data.
            if fm.fileExists(atPath:old.path) && fm.fileExists(atPath:shared.path) { return privateRoot }
            // Finish fallible catalog work before moving existing user data.
            if !fm.fileExists(atPath:sharedRoot.appendingPathComponent("roots.json").path) { try RootfsCatalog.alpine.save(sharedRoot) }
            if fm.fileExists(atPath:old.path) { try fm.moveItem(at:old,to:shared) }
        } catch {
            if fm.fileExists(atPath:old.path) || !fm.fileExists(atPath:shared.path) { return privateRoot }
            throw error
        }
        return sharedRoot
    }
}
struct RootfsEntry {
    let path: Data
    let inode: UInt64
    let kind: UInt8
    let mode: UInt32
    let modified: Date
    var filename: String { String(decoding:path.split(separator:47).last ?? Data.SubSequence(),as:UTF8.self) }
    var parent: Data { guard let index=path.lastIndex(of:47),index>0 else { return Data([47]) };return Data(path[..<index]) }
    func identifier(environment:String) -> String { "node/\(environment)/\(String(inode,radix:16))/\(String(RootfsSnapshot.hash(path),radix:16))" }
}
/// Reads the existing atomic v3/v4 manifest without taking its writer lock.
/// No VFS instance is opened, no recovery runs and no namespace is modified.
struct RootfsSnapshot {
    let store: URL
    let revision: UInt64
    let entries: [Data:RootfsEntry]
    static func hash(_ data:Data) -> UInt64 { data.reduce(0xcbf29ce484222325) { ($0 ^ UInt64($1)) &* 0x100000001b3 } }
    init(store:URL) throws {
        let data=try Data(contentsOf:store.appendingPathComponent("manifest"),options:.mappedIfSafe)
        guard data.count>=48,data.count<=128*1024*1024 else { throw RootfsCatalogError.invalidStore }
        var tail=Reader(data:Data(data.suffix(8)))
        let checksum=try tail.uint(8)
        let body=Data(data.dropLast(8));guard Self.hash(body)==checksum else { throw RootfsCatalogError.invalidStore }
        var r=Reader(data:body)
        guard try r.take(8)==Data("TRAMESFS".utf8) else { throw RootfsCatalogError.invalidStore }
        let version=try r.uint(4);guard version==3 || version==4 else { throw RootfsCatalogError.invalidStore }
        _=try r.uint(8);let limit=try r.uint(4);let next=try r.uint(8)
        let count=try r.uint(4),paths=try r.uint(4)
        guard limit<=1_000_000,count>0,count<=limit,paths<=limit else { throw RootfsCatalogError.invalidStore }
        struct Node { let inode:UInt64;let kind:UInt8;let mode:UInt32;let modified:Date }
        var nodes:[UInt64:Node]=[:]
        for _ in 0..<count {
            let inode=try r.uint(8),mode=UInt32(try r.uint(4));_=try r.take(8)
            let kind=try r.take(4);guard let tag=kind.first,tag<=8,kind.dropFirst().allSatisfy({$0==0}),inode>0,inode<next,nodes[inode]==nil else { throw RootfsCatalogError.invalidStore }
            _=try r.take(16);let seconds=Int64(bitPattern:try r.uint(8));let nanos=try r.uint(4);_=try r.take(20)
            guard nanos<1_000_000_000 else { throw RootfsCatalogError.invalidStore }
            let size=try r.uint(4);guard size<4096 else { throw RootfsCatalogError.invalidStore };_=try r.take(Int(size))
            nodes[inode]=Node(inode:inode,kind:tag,mode:mode,modified:Date(timeIntervalSince1970:Double(seconds)+Double(nanos)/1_000_000_000))
        }
        if version==4 {
            let count=try r.uint(4);guard count<=nodes.count else { throw RootfsCatalogError.invalidStore }
            for _ in 0..<count {
                let inode=try r.uint(8),count=try r.uint(4);guard nodes[inode] != nil,count>0,count<=128 else { throw RootfsCatalogError.invalidStore }
                for _ in 0..<count { let name=try r.uint(4),value=try r.uint(4);guard name<=255,value<=65536 else { throw RootfsCatalogError.invalidStore };_=try r.take(Int(name+value)) }
            }
        }
        var entries:[Data:RootfsEntry]=[:]
        for _ in 0..<paths {
            let length=try r.uint(4),inode=try r.uint(8)
            guard length>0,length<4096,let node=nodes[inode] else { throw RootfsCatalogError.invalidStore }
            let path=try r.take(Int(length));let parts=path.dropFirst().split(separator:47,omittingEmptySubsequences:false)
            guard path.first==47,!path.contains(0),entries[path]==nil,path==Data([47]) || parts.allSatisfy({!$0.isEmpty && $0.count<=255 && $0 != Data(".".utf8) && $0 != Data("..".utf8)}) else { throw RootfsCatalogError.invalidStore }
            entries[path]=RootfsEntry(path:path,inode:inode,kind:node.kind,mode:node.mode,modified:node.modified)
        }
        guard r.offset==body.count,entries[Data([47])]?.kind==0 else { throw RootfsCatalogError.invalidStore }
        for entry in entries.values where entry.path != Data([47]) { guard entries[entry.parent]?.kind==0 else { throw RootfsCatalogError.invalidStore } }
        self.store=store;self.revision=checksum;self.entries=entries
    }
    func object(_ entry:RootfsEntry) throws -> URL {
        guard entry.kind==1 else { throw RootfsCatalogError.noSuchItem }
        return store.appendingPathComponent("objects",isDirectory:true).appendingPathComponent(String(format:"%016llx.dat",entry.inode))
    }
    func materialize(_ entry:RootfsEntry,to destination:URL) throws {
        let source=try object(entry)
        let fd=open(source.path,O_RDONLY|O_NOFOLLOW)
        guard fd>=0 else { throw RootfsCatalogError.noSuchItem }
        let input=FileHandle(fileDescriptor:fd,closeOnDealloc:true);defer { try? input.close() }
        var stat=Darwin.stat();guard fstat(fd,&stat)==0,(stat.st_mode & S_IFMT)==S_IFREG else { throw RootfsCatalogError.invalidStore }
        let stage=destination.deletingLastPathComponent().appendingPathComponent(".download-\(UUID().uuidString)")
        guard FileManager.default.createFile(atPath:stage.path,contents:nil) else { throw RootfsCatalogError.unavailable }
        defer { try? FileManager.default.removeItem(at:stage) }
        let output=try FileHandle(forWritingTo:stage);defer { try? output.close() }
        var remaining=UInt64(stat.st_size)
        while remaining>0 {
            let data=try input.read(upToCount:Int(min(remaining,65536))) ?? Data()
            guard !data.isEmpty else { throw RootfsCatalogError.unavailable }
            try output.write(contentsOf:data);remaining-=UInt64(data.count)
        }
        // Provider cache only: the original inode and rootfs are always read-only.
        if FileManager.default.fileExists(atPath:destination.path) { _=try FileManager.default.replaceItemAt(destination,withItemAt:stage) }
        else { try FileManager.default.moveItem(at:stage,to:destination) }
    }
    private struct Reader {
        let data:Data;var offset=0
        mutating func take(_ count:Int) throws -> Data {
            guard count>=0,offset<=data.count,count<=data.count-offset else { throw RootfsCatalogError.invalidStore }
            defer { offset+=count };return Data(data[offset..<offset+count])
        }
        mutating func uint(_ count:Int) throws -> UInt64 {
            try take(count).enumerated().reduce(0) { $0 | UInt64($1.element) << ($1.offset*8) }
        }
    }
}
