// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
import CryptoKit
import Darwin

struct RootfsFileVersion:Codable,Equatable {
    let inode:UInt64
    let sha256:String
}
struct RootfsEditBase:Codable {
    let identifier:String
    let environment:String
    let path:String
    var version:RootfsFileVersion
    var error:String?
}
struct RootfsEditRequest:Codable {
    let token:String
    let base:RootfsEditBase
    let sha256:String
    let size:UInt64
    var error:String?
}

/// Shared-container journal only. The extension never opens a writable VFS.
/// Locks are per edited document and are acquired only for materialization/save
/// or foreground replay; there is no guest-path hook or background scanner.
struct RootfsEditStore {
    let root:URL
    static var savedNotification:CFString? {RootfsLocations.group.map {($0+".file-provider-edit-saved") as CFString}}
    static func signalSaved() {
        guard let name=savedNotification else {return}
        CFNotificationCenterPostNotification(CFNotificationCenterGetDarwinNotifyCenter(),CFNotificationName(name),nil,nil,true)
    }
    private var states:URL {root.appendingPathComponent("file-provider-state",isDirectory:true)}
    private var pending:URL {root.appendingPathComponent("file-provider-pending",isDirectory:true)}
    private func key(_ id:String) -> String {SHA256.hash(data:Data(id.utf8)).map {String(format:"%02x",$0)}.joined()}
    private func stateURL(_ id:String) -> URL {states.appendingPathComponent(key(id)+".json")}
    private func folder(_ id:String) -> URL {pending.appendingPathComponent(key(id),isDirectory:true)}
    private func decode<T:Decodable>(_ type:T.Type,_ url:URL) throws -> T {
        let data=try Data(contentsOf:url)
        guard data.count<=65536 else {throw RootfsCatalogError.invalidStore}
        return try JSONDecoder().decode(type,from:data)
    }
    private func save<T:Encodable>(_ value:T,_ url:URL) throws {
        try FileManager.default.createDirectory(at:url.deletingLastPathComponent(),withIntermediateDirectories:true)
        try JSONEncoder().encode(value).write(to:url,options:.atomic)
    }
    private func locked<T>(_ id:String,_ operation:() throws -> T) throws -> T {
        let locks=root.appendingPathComponent("file-provider-locks",isDirectory:true)
        try FileManager.default.createDirectory(at:locks,withIntermediateDirectories:true)
        let fd=open(locks.appendingPathComponent(key(id)+".lock").path,O_RDWR|O_CREAT|O_NOFOLLOW,0o600)
        guard fd>=0 else {throw RootfsCatalogError.unavailable}
        defer {close(fd)}
        guard flock(fd,LOCK_EX)==0 else {throw RootfsCatalogError.unavailable}
        defer {flock(fd,LOCK_UN)}
        return try operation()
    }
    func base(_ id:String) -> RootfsEditBase? {try? decode(RootfsEditBase.self,stateURL(id))}
    func request(_ id:String) -> RootfsEditRequest? {
        guard let value=try? decode(RootfsEditRequest.self,folder(id).appendingPathComponent("request.json")),
              value.base.identifier==id,UUID(uuidString:value.token) != nil,
              value.base.path.hasPrefix("/"),!value.base.path.utf8.contains(0) else {return nil}
        return value
    }
    func payload(_ request:RootfsEditRequest) -> URL {
        folder(request.base.identifier).appendingPathComponent(request.token)
    }
    func requests() -> [RootfsEditRequest] {
        let directories=(try? FileManager.default.contentsOfDirectory(at:pending,includingPropertiesForKeys:nil)) ?? []
        return directories.compactMap { directory in
            guard let seed=try? decode(RootfsEditRequest.self,directory.appendingPathComponent("request.json")),directory.lastPathComponent==key(seed.base.identifier) else {return nil}
            return request(seed.base.identifier)
        }.sorted {$0.base.identifier<$1.base.identifier}
    }
    func revision() -> String {
        let data=Data(requests().map {$0.token+($0.error ?? "")}.joined(separator:";").utf8)
        return SHA256.hash(data:data).map {String(format:"%02x",$0)}.joined()
    }
    func record(_ base:RootfsEditBase) throws {
        try locked(base.identifier) {
            if request(base.identifier)==nil {try save(base,stateURL(base.identifier))}
        }
    }
    func retainError(_ id:String,_ error:Error) {
        try? locked(id) {
            guard var value=base(id) else {return}
            value.error=error.localizedDescription;try save(value,stateURL(id))
        }
    }
    func preservesLocalEdit(_ id:String,at url:URL) throws -> Bool {
        guard FileManager.default.fileExists(atPath:url.path) else {return false}
        if request(id) != nil {return true}
        guard let base=base(id) else {return false}
        if base.error != nil {return true}
        return try Self.checksum(url) != base.version.sha256
    }
    func stage(_ id:String,from source:URL) throws {
        try locked(id) {
            guard let base=base(id) else {throw RootfsCatalogError.unavailable}
            let old=request(id),token=UUID().uuidString
            let directory=folder(id),destination=directory.appendingPathComponent(token)
            let existed=FileManager.default.fileExists(atPath:directory.path)
            try FileManager.default.createDirectory(at:directory,withIntermediateDirectories:true)
            do {
                let copied=try Self.copy(source,to:destination,durable:true)
                if copied.sha256==(old?.base ?? base).version.sha256 {
                    try FileManager.default.removeItem(at:directory)
                    var clean=base;clean.error=nil;try save(clean,stateURL(id))
                    return
                }
                let value=RootfsEditRequest(token:token,base:old?.base ?? base,sha256:copied.sha256,size:copied.size)
                try save(value,directory.appendingPathComponent("request.json"))
                if base.error != nil {var clean=base;clean.error=nil;try save(clean,stateURL(id))}
                if let old {try? FileManager.default.removeItem(at:payload(old))}
            } catch {
                try? FileManager.default.removeItem(at:destination)
                if !existed {try? FileManager.default.removeItem(at:directory)}
                throw error
            }
        }
    }
    func materializePending(_ id:String,to destination:URL) throws -> Bool {
        try locked(id) {
            guard let value=request(id) else {return false}
            _=try Self.copy(payload(value),to:destination);return true
        }
    }
    /// Called once before initial guest execution and on foreground resumption.
    /// The callback performs conditional VFS replacement and returns its version.
    func replay(_ apply:(RootfsEditRequest,URL) throws -> RootfsFileVersion) -> [String] {
        guard let directories=try? FileManager.default.contentsOfDirectory(at:pending,includingPropertiesForKeys:nil) else {return []}
        var errors:[String]=[]
        for directory in directories {
            do {
                // A foreground transition can race the streaming stage, before
                // request.json is published. The stable baseline supplies the
                // document lock, so replay waits for that save to finish.
                let seed=try decode(RootfsEditBase.self,states.appendingPathComponent(directory.lastPathComponent+".json"))
                guard directory.lastPathComponent==key(seed.identifier) else {throw RootfsCatalogError.invalidStore}
                try locked(seed.identifier) {
                    guard var value=request(seed.identifier) else {return}
                    do {
                        let version=try apply(value,payload(value))
                        var base=value.base;base.version=version;base.error=nil
                        try save(base,stateURL(base.identifier))
                        try FileManager.default.removeItem(at:folder(base.identifier))
                    } catch {
                        value.error=error.localizedDescription
                        try save(value,directory.appendingPathComponent("request.json"))
                        errors.append("\(value.base.path): \(error.localizedDescription)")
                    }
                }
            } catch {errors.append(error.localizedDescription)}
        }
        return errors
    }
    /// Bounded streaming; hashing is paid only when Files materializes/saves.
    static func checksum(_ url:URL) throws -> String {
        let fd=open(url.path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK)
        guard fd>=0 else {throw RootfsCatalogError.noSuchItem}
        let input=FileHandle(fileDescriptor:fd,closeOnDealloc:true);defer {try? input.close()}
        var info=Darwin.stat()
        guard fstat(fd,&info)==0,(info.st_mode&S_IFMT)==S_IFREG else {throw RootfsCatalogError.invalidStore}
        var hash=SHA256()
        while true {
            let count=try autoreleasepool {
                let data=try input.read(upToCount:65536) ?? Data()
                hash.update(data:data);return data.count
            }
            if count==0 {break}
        }
        return hash.finalize().map {String(format:"%02x",$0)}.joined()
    }
    @discardableResult
    static func copy(_ source:URL,to destination:URL,durable:Bool=false) throws -> (sha256:String,size:UInt64) {
        let fd=open(source.path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK)
        guard fd>=0 else {throw RootfsCatalogError.noSuchItem}
        let input=FileHandle(fileDescriptor:fd,closeOnDealloc:true);defer {try? input.close()}
        var info=Darwin.stat()
        guard fstat(fd,&info)==0,(info.st_mode&S_IFMT)==S_IFREG else {throw RootfsCatalogError.invalidStore}
        let stage=destination.deletingLastPathComponent().appendingPathComponent(".edit-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at:stage.deletingLastPathComponent(),withIntermediateDirectories:true)
        guard FileManager.default.createFile(atPath:stage.path,contents:nil) else {throw RootfsCatalogError.unavailable}
        defer {try? FileManager.default.removeItem(at:stage)}
        let output=try FileHandle(forWritingTo:stage);defer {try? output.close()}
        var remaining=UInt64(info.st_size),hash=SHA256()
        while remaining>0 {
            // FileHandle's bridged NSData is autoreleased on Apple platforms.
            // Drain each chunk; a 64 KiB read bound alone still retains O(file)
            // temporary memory until the extension's runloop next drains.
            let count=try autoreleasepool {
                let data=try input.read(upToCount:Int(min(remaining,65536))) ?? Data()
                guard !data.isEmpty else {throw RootfsCatalogError.unavailable}
                try output.write(contentsOf:data);hash.update(data:data);return data.count
            }
            remaining-=UInt64(count)
        }
        if durable {try output.synchronize()}
        if FileManager.default.fileExists(atPath:destination.path) {_=try FileManager.default.replaceItemAt(destination,withItemAt:stage)}
        else {try FileManager.default.moveItem(at:stage,to:destination)}
        return (hash.finalize().map {String(format:"%02x",$0)}.joined(),UInt64(info.st_size))
    }
}
