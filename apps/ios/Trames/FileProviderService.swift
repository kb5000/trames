// SPDX-License-Identifier: GPL-3.0-or-later
import FileProvider
import Foundation
import UIKit
import TramesKit
@MainActor
enum FileProviderService {
    private static var lastCheck:TimeInterval=0
    private static var lastVersion=""
    private static var replay:(id:UUID,task:Task<[String],Never>)?
    private static let observer=NSObject()
    private static var onSave:(()->Void)?
    private static var observing=false
    private static var saveGeneration:UInt64=0
    static func observeSaves(_ action:@escaping()->Void) {
        onSave=action
        guard !observing,let name=RootfsEditStore.savedNotification else {return}
        observing=true
        CFNotificationCenterAddObserver(CFNotificationCenterGetDarwinNotifyCenter(),Unmanaged.passUnretained(observer).toOpaque(),{ _,_,_,_,_ in
            Task { @MainActor in
                FileProviderService.saveGeneration &+= 1
                if UIApplication.shared.applicationState == .active {FileProviderService.onSave?()}
            }
        },name,nil,.deliverImmediately)
    }
    static func hasPendingEdits() -> Bool {
        guard let root=RootfsLocations.sharedRoot else {return false}
        let directory=root.appendingPathComponent("file-provider-pending",isDirectory:true)
        return !((try? FileManager.default.contentsOfDirectory(atPath:directory.path)) ?? []).isEmpty
    }
    static func applyPending(to local:LocalSession) async -> [String] {
        if let replay {return await replay.task.value}
        guard UIApplication.shared.applicationState == .active,UserDefaults.standard.bool(forKey:"rootfsFileProviderAvailable"),let root=RootfsLocations.sharedRoot else {return []}
        let id=UUID()
        let task=Task { @MainActor () -> [String] in
            defer {if replay?.id==id {replay=nil}}
            var errors=Set<String>()
            repeat {
                let generation=saveGeneration
                let failures=await Task.detached(priority:.userInitiated) {
                    RootfsEditStore(root:root).replay { request,payload in
                        let catalog=try RootfsCatalog.load(root)
                        guard let environment=catalog.environments.first(where:{$0.id==request.base.environment}),environment.id=="alpine" else {throw RootfsCatalogError.noSuchItem}
                        let inode=try local.commitFileEdit(guest:request.base.path,host:payload,expectedInode:request.base.version.inode,expectedSHA256:request.base.version.sha256,replacementSHA256:request.sha256)
                        return RootfsFileVersion(inode:inode,sha256:request.sha256)
                    }
                }.value
                errors.formUnion(failures)
                if generation==saveGeneration || UIApplication.shared.applicationState != .active {break}
            } while true
            lastVersion="";poll()
            return errors.sorted()
        }
        replay=(id,task)
        return await task.value
    }
    static func poll() {
        let now=ProcessInfo.processInfo.systemUptime
        guard now-lastCheck>=2,UserDefaults.standard.bool(forKey:"rootfsFileProviderAvailable"),let root=RootfsLocations.sharedRoot else {return}
        lastCheck=now
        guard let catalog=try? RootfsCatalog.load(root) else {return}
        let version=catalog.environments.compactMap { environment -> String? in
            guard let url=try? environment.storeURL(in:root),let a=try? FileManager.default.attributesOfItem(atPath:url.appendingPathComponent("manifest").path) else {return nil}
            return "\(environment.id)/\(a[.systemFileNumber] ?? "")/\(a[.modificationDate] ?? "")/\(a[.size] ?? "")"
        }.joined(separator:";")
        guard version != lastVersion else {return};lastVersion=version
        let observed=(try? JSONDecoder().decode([String].self,from:Data(contentsOf:root.appendingPathComponent("observed-folders.json")))) ?? []
        for id in Set(observed.prefix(64)).union([NSFileProviderItemIdentifier.rootContainer.rawValue,NSFileProviderItemIdentifier.workingSet.rawValue]) {
            NSFileProviderManager.default.signalEnumerator(for:NSFileProviderItemIdentifier(rawValue:id)) {_ in}
        }
    }
}
