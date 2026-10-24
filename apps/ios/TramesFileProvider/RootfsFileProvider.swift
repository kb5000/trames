// SPDX-License-Identifier: GPL-3.0-or-later
import FileProvider
import Foundation
import UniformTypeIdentifiers

private final class RootfsItem: NSObject,NSFileProviderItem {
    let itemIdentifier: NSFileProviderItemIdentifier
    let parentItemIdentifier: NSFileProviderItemIdentifier
    let filename:String
    let directory:Bool
    let readable:Bool
    let writable:Bool
    let pending:Bool
    let uploadingError:Error?
    var isUploaded:Bool {!pending && uploadingError==nil}
    var isUploading:Bool {false}
    let documentSize:NSNumber?
    let contentModificationDate:Date?
    let itemVersion:NSFileProviderItemVersion
    var typeIdentifier:String { directory ? UTType.folder.identifier : (UTType(filenameExtension:(filename as NSString).pathExtension)?.identifier ?? UTType.data.identifier) }
    var capabilities:NSFileProviderItemCapabilities { directory ? [.allowsReading,.allowsContentEnumerating] : (readable ? (writable ? [.allowsReading,.allowsWriting] : [.allowsReading]) : []) }
    init(id:String,parent:String,name:String,directory:Bool,readable:Bool=true,writable:Bool=false,pending:Bool=false,error:Error?=nil,size:UInt64?=nil,modified:Date?=nil,version:String="1") {
        itemIdentifier=NSFileProviderItemIdentifier(rawValue:id);parentItemIdentifier=NSFileProviderItemIdentifier(rawValue:parent)
        filename=name;self.directory=directory;self.readable=readable;documentSize=size.map(NSNumber.init(value:));contentModificationDate=modified
        self.writable=writable;self.pending=pending;uploadingError=error
        itemVersion=NSFileProviderItemVersion(contentVersion:Data(version.utf8),metadataVersion:Data(version.utf8))
    }
}
private struct RootfsIndex {
    let environment:RootfsEnvironment
    let snapshot:RootfsSnapshot
    let byID:[String:RootfsEntry]
    let children:[Data:[RootfsEntry]]
}
@objc(RootfsFileProvider)
final class RootfsFileProvider:NSFileProviderExtension {
    private let lock=NSRecursiveLock()
    private var cache:[String:(String,RootfsIndex)]=[:]
    private var storage:URL { NSFileProviderManager.default.documentStorageURL }
    private func edits() throws -> RootfsEditStore {RootfsEditStore(root:try root())}
    private func root() throws -> URL {
        guard let root=RootfsLocations.sharedRoot else { throw NSFileProviderError(.notAuthenticated) };return root
    }
    private func catalog() throws -> RootfsCatalog { try RootfsCatalog.load(root()) }
    private func index(_ environment:RootfsEnvironment) throws -> RootfsIndex {
        let store=try environment.storeURL(in:root())
        let attributes=try FileManager.default.attributesOfItem(atPath:store.appendingPathComponent("manifest").path)
        let signature="\(attributes[.systemFileNumber] ?? "")/\(attributes[.modificationDate] ?? "")/\(attributes[.size] ?? "")"
        lock.lock();defer {lock.unlock()}
        if let old=cache[environment.id],old.0==signature {return old.1}
        let snapshot=try RootfsSnapshot(store:store)
        var ids:[String:RootfsEntry]=[:],children:[Data:[RootfsEntry]]=[:]
        for entry in snapshot.entries.values where entry.path != Data([47]) {
            let id=entry.identifier(environment:environment.id)
            guard ids.updateValue(entry,forKey:id)==nil else {throw RootfsCatalogError.invalidStore}
            children[entry.parent,default:[]].append(entry)
        }
        let result=RootfsIndex(environment:environment,snapshot:snapshot,byID:ids,children:children)
        if cache.count>=4 {cache.removeAll()};cache[environment.id]=(signature,result);return result
    }
    private func environment(_ id:String) throws -> RootfsEnvironment {
        guard let environment=try catalog().environments.first(where:{$0.id==id}) else {throw NSFileProviderError(.noSuchItem)};return environment
    }
    private func node(_ id:NSFileProviderItemIdentifier) throws -> (RootfsIndex,RootfsEntry) {
        let parts=id.rawValue.split(separator:"/")
        guard parts.count==4,parts[0]=="node" else {throw NSFileProviderError(.noSuchItem)}
        let index=try index(environment(String(parts[1])))
        guard let entry=index.byID[id.rawValue] else {throw NSFileProviderError(.noSuchItem)}
        return (index,entry)
    }
    private func item(_ index:RootfsIndex,_ entry:RootfsEntry) throws -> RootfsItem {
        let parent:String
        if entry.parent==Data([47]) {parent="environment/"+index.environment.id}
        else {guard let node=index.snapshot.entries[entry.parent] else {throw NSFileProviderError(.noSuchItem)};parent=node.identifier(environment:index.environment.id)}
        let attributes=entry.kind==1 ? try? FileManager.default.attributesOfItem(atPath:index.snapshot.object(entry).path) : nil
        let size=(attributes?[.size] as? NSNumber)?.uint64Value
        let modified=attributes?[.modificationDate] as? Date ?? entry.modified
        let id=entry.identifier(environment:index.environment.id),journal=try edits(),pending=journal.request(id)
        let problem=pending?.error ?? journal.base(id)?.error
        let error=problem.map {NSError(domain:NSFileProviderErrorDomain,code:NSFileProviderError.cannotSynchronize.rawValue,userInfo:[NSLocalizedDescriptionKey:$0])}
        return RootfsItem(id:id,parent:parent,name:entry.filename,directory:entry.kind==0,readable:entry.kind==0 || entry.kind==1,writable:index.environment.id=="alpine" && entry.kind==1 && String(data:entry.path,encoding:.utf8) != nil,pending:pending != nil,error:error,size:pending?.size ?? size,modified:entry.modified,version:pending?.token ?? "\(index.snapshot.revision)/\(modified.timeIntervalSince1970)/\(size ?? 0)")
    }
    override func item(for identifier:NSFileProviderItemIdentifier) throws -> NSFileProviderItem {
        if identifier == .rootContainer {return RootfsItem(id:identifier.rawValue,parent:identifier.rawValue,name:"Trames",directory:true)}
        if identifier.rawValue=="pending-edits" {return RootfsItem(id:"pending-edits",parent:NSFileProviderItemIdentifier.rootContainer.rawValue,name:"待写回的编辑",directory:true)}
        if identifier.rawValue.hasPrefix("pending/") {
            guard let request=try edits().requests().first(where:{"pending/"+$0.token==identifier.rawValue}) else {throw NSFileProviderError(.noSuchItem)}
            return recoveryItem(request)
        }
        if identifier.rawValue.hasPrefix("environment/") {
            let environment=try environment(String(identifier.rawValue.dropFirst("environment/".count)))
            let index=try index(environment)
            return RootfsItem(id:identifier.rawValue,parent:NSFileProviderItemIdentifier.rootContainer.rawValue,name:environment.name,directory:true,version:String(index.snapshot.revision))
        }
        let (index,entry)=try node(identifier);return try item(index,entry)
    }
    private func recoveryItem(_ request:RootfsEditRequest) -> RootfsItem {
        let name=URL(fileURLWithPath:request.base.path).lastPathComponent as NSString
        let suffix=name.pathExtension.isEmpty ? "" : "."+name.pathExtension
        let filename="\(name.deletingPathExtension) (\(request.token.prefix(8)))"+suffix
        return RootfsItem(id:"pending/"+request.token,parent:"pending-edits",name:filename,directory:false,size:request.size,version:request.token)
    }
    fileprivate func listing(_ identifier:NSFileProviderItemIdentifier) throws -> [NSFileProviderItem] {
        if identifier == .workingSet || identifier.rawValue=="pending-edits" {return try edits().requests().map {recoveryItem($0)}}
        if identifier == .rootContainer {
            var items=try catalog().environments.map {try item(for:NSFileProviderItemIdentifier(rawValue:"environment/"+$0.id))}
            if try !edits().requests().isEmpty {items.append(try item(for:NSFileProviderItemIdentifier(rawValue:"pending-edits")))}
            return items
        }
        let index:RootfsIndex,path:Data
        if identifier.rawValue.hasPrefix("environment/") {
            index=try self.index(environment(String(identifier.rawValue.dropFirst("environment/".count))));path=Data([47])
        } else {
            let pair=try node(identifier);index=pair.0;path=pair.1.path
            if pair.1.kind != 0 {return [try item(index,pair.1)]}
        }
        return try (index.children[path] ?? []).sorted { $0.path.lexicographicallyPrecedes($1.path) }.map {try item(index,$0)}
    }
    fileprivate func anchor() throws -> NSFileProviderSyncAnchor {
        let version=try catalog().environments.map {try "\($0.id):\(index($0).snapshot.revision)"}.joined(separator:";")
        return NSFileProviderSyncAnchor(rawValue:Data(("editable-v1:"+version+":"+(try edits().revision())).utf8))
    }
    override func enumerator(for containerItemIdentifier:NSFileProviderItemIdentifier) throws -> NSFileProviderEnumerator {
        _=try listing(containerItemIdentifier)
        if let root=RootfsLocations.sharedRoot {
            lock.lock();defer {lock.unlock()}
            let url=root.appendingPathComponent("observed-folders.json")
            var observed=(try? JSONDecoder().decode([String].self,from:Data(contentsOf:url))) ?? []
            observed.removeAll {$0==containerItemIdentifier.rawValue};observed.append(containerItemIdentifier.rawValue)
            try? JSONEncoder().encode(Array(observed.suffix(64))).write(to:url,options:.atomic)
        }
        return RootfsEnumerator(provider:self,container:containerItemIdentifier)
    }
    override func urlForItem(withPersistentIdentifier identifier:NSFileProviderItemIdentifier) -> URL? {
        if identifier == .rootContainer {return storage}
        guard let item=try? item(for:identifier) else {return nil}
        let encoded=Data(identifier.rawValue.utf8).base64EncodedString().replacingOccurrences(of:"/",with:"_").replacingOccurrences(of:"+",with:"-")
        return storage.appendingPathComponent(encoded,isDirectory:true).appendingPathComponent(item.filename,isDirectory:item.typeIdentifier==UTType.folder.identifier)
    }
    override func persistentIdentifierForItem(at url:URL) -> NSFileProviderItemIdentifier? {
        guard let parts=RootfsProviderPath.relativeComponents(of:url,in:storage) else {return nil}
        if parts.isEmpty {return .rootContainer}
        guard parts.count==2,let data=Data(base64Encoded:String(parts[0]).replacingOccurrences(of:"_",with:"/").replacingOccurrences(of:"-",with:"+")),let value=String(data:data,encoding:.utf8) else {return nil}
        let id=NSFileProviderItemIdentifier(rawValue:value)
        guard let expected=urlForItem(withPersistentIdentifier:id),RootfsProviderPath.sameLocation(expected,url) else {return nil};return id
    }
    override func providePlaceholder(at url:URL,completionHandler:@escaping(Error?)->Void) {
        do {
            guard let id=persistentIdentifierForItem(at:url) else {throw NSFileProviderError(.noSuchItem)}
            let item=try item(for:id)
            try FileManager.default.createDirectory(at:url.deletingLastPathComponent(),withIntermediateDirectories:true)
            try NSFileProviderManager.writePlaceholder(at:NSFileProviderManager.placeholderURL(for:url),withMetadata:item)
            completionHandler(nil)
        } catch {completionHandler(error)}
    }
    override func startProvidingItem(at url:URL,completionHandler:@escaping(Error?)->Void) {
        do {
            guard let id=persistentIdentifierForItem(at:url) else {throw NSFileProviderError(.noSuchItem)}
            let metadata=try item(for:id)
            try FileManager.default.createDirectory(at:url.deletingLastPathComponent(),withIntermediateDirectories:true)
            if metadata.typeIdentifier==UTType.folder.identifier {try FileManager.default.createDirectory(at:url,withIntermediateDirectories:true)}
            else {
                let journal=try edits()
                if id.rawValue.hasPrefix("pending/") {
                    guard let request=journal.requests().first(where:{"pending/"+$0.token==id.rawValue}) else {throw NSFileProviderError(.noSuchItem)}
                    _=try RootfsEditStore.copy(journal.payload(request),to:url)
                } else if try journal.preservesLocalEdit(id.rawValue,at:url) {
                    // Coordination may reenter materialization before the save
                    // callback. Never download over modified or pending bytes.
                } else if try !journal.materializePending(id.rawValue,to:url) {
                    let (index,entry)=try node(id)
                    let version=try index.snapshot.materialize(entry,to:url)
                    if let path=String(data:entry.path,encoding:.utf8) {
                        try journal.record(RootfsEditBase(identifier:id.rawValue,environment:index.environment.id,path:path,version:version))
                    }
                }
            }
            completionHandler(nil)
        } catch {completionHandler(error)}
    }
    override func stopProvidingItem(at url:URL) {
        guard let id=persistentIdentifierForItem(at:url),let item=try? item(for:id),item.typeIdentifier != UTType.folder.identifier else {return}
        guard let journal=try? edits(),journal.request(id.rawValue)==nil,journal.base(id.rawValue)?.error==nil else {return}
        if item.capabilities?.contains(.allowsWriting)==true {
            guard let base=journal.base(id.rawValue),(try? RootfsEditStore.checksum(url))==base.version.sha256 else {return}
        }
        try? FileManager.default.removeItem(at:url)
        providePlaceholder(at:url) {_ in}
    }
    override func itemChanged(at url:URL) {
        guard let id=persistentIdentifierForItem(at:url),let journal=try? edits() else {return}
        do {
            var coordinationError:NSError?,copyError:Error?
            let coordinator=NSFileCoordinator()
            coordinator.purposeIdentifier=NSFileProviderManager.default.providerIdentifier
            coordinator.coordinate(readingItemAt:url,options:.withoutChanges,error:&coordinationError) { source in
                do {try journal.stage(id.rawValue,from:source)} catch {copyError=error}
            }
            if let error=coordinationError {throw error}
            if let error=copyError {throw error}
        } catch {journal.retainError(id.rawValue,error)}
        RootfsEditStore.signalSaved()
        let parent=(try? item(for:id).parentItemIdentifier) ?? .rootContainer
        NSFileProviderManager.default.signalEnumerator(for:parent) {_ in}
        NSFileProviderManager.default.signalEnumerator(for:.rootContainer) {_ in}
        NSFileProviderManager.default.signalEnumerator(for:.workingSet) {_ in}
    }
}
private final class RootfsEnumerator:NSObject,NSFileProviderEnumerator {
    let provider:RootfsFileProvider,container:NSFileProviderItemIdentifier
    private var previous:Set<NSFileProviderItemIdentifier>=[]
    init(provider:RootfsFileProvider,container:NSFileProviderItemIdentifier) {self.provider=provider;self.container=container}
    func invalidate() {}
    func enumerateItems(for observer:NSFileProviderEnumerationObserver,startingAt page:NSFileProviderPage) {
        do {
            let items=try provider.listing(container)
            let pageText=String(data:page.rawValue,encoding:.utf8) ?? ""
            let offset=pageText.hasPrefix("offset:") ? (Int(pageText.dropFirst(7)) ?? -1) : 0
            guard offset>=0,offset<=items.count else {throw NSFileProviderError(.noSuchItem)}
            let end=min(offset+256,items.count)
            observer.didEnumerate(Array(items[offset..<end]));previous.formUnion(items.map(\.itemIdentifier))
            observer.finishEnumerating(upTo:end<items.count ? NSFileProviderPage(rawValue:Data("offset:\(end)".utf8)) : nil)
        } catch {observer.finishEnumeratingWithError(error)}
    }
    func enumerateChanges(for observer:NSFileProviderChangeObserver,from syncAnchor:NSFileProviderSyncAnchor) {
        do {
            let anchor=try provider.anchor()
            if anchor != syncAnchor {
                // Enumerator instances can be recreated; an unknown previous set
                // cannot safely report deletions. Ask Files for a fresh listing.
                guard !previous.isEmpty else {throw NSFileProviderError(.syncAnchorExpired)}
                let items=try provider.listing(container),ids=Set(items.map(\.itemIdentifier))
                observer.didDeleteItems(withIdentifiers:Array(previous.subtracting(ids)));observer.didUpdate(items);previous=ids
            }
            observer.finishEnumeratingChanges(upTo:anchor,moreComing:false)
        } catch {observer.finishEnumeratingWithError(error)}
    }
    func currentSyncAnchor(completionHandler:@escaping(NSFileProviderSyncAnchor?)->Void) {completionHandler(try? provider.anchor())}
}
