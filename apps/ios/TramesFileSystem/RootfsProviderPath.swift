// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation

enum RootfsProviderPath {
    // File-provider URLs name placeholders that may not exist yet. Foundation's
    // standardizedFileURL can strip /private only from existing paths, so using
    // it for containment makes the same URL change identity after materializing.
    private static func components(_ url:URL) -> [String]? {
        var parts=url.pathComponents
        guard !parts.contains("."),!parts.contains("..") else {return nil}
        // iOS sends save callbacks under /var while documentStorageURL uses
        // /private/var. Normalize only Darwin's system aliases, without asking
        // Foundation to resolve a placeholder whose existence can change.
        if parts.count>=3,parts[1]=="private",["var","tmp","etc"].contains(parts[2]) {parts.remove(at:1)}
        return parts
    }
    static func relativeComponents(of url:URL,in storage:URL) -> [String]? {
        guard url.isFileURL,storage.isFileURL,url.host==storage.host else {return nil}
        guard let base=components(storage),let parts=components(url),parts.starts(with:base) else {return nil}
        return Array(parts.dropFirst(base.count))
    }
    static func sameLocation(_ first:URL,_ second:URL) -> Bool {
        guard first.isFileURL,second.isFileURL,first.host==second.host,
              let left=components(first),let right=components(second) else {return false}
        return left==right
    }
}
