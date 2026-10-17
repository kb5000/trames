// SPDX-License-Identifier: GPL-3.0-or-later
import FileProvider
import Foundation
@MainActor
enum FileProviderService {
    private static var lastCheck:TimeInterval=0
    private static var lastVersion=""
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
        for id in Set(observed.prefix(64)).union([NSFileProviderItemIdentifier.rootContainer.rawValue]) {
            NSFileProviderManager.default.signalEnumerator(for:NSFileProviderItemIdentifier(rawValue:id)) {_ in}
        }
    }
}
