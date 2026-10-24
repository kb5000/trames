// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation

enum RootfsProviderPath {
    // File-provider URLs name placeholders that may not exist yet. Foundation's
    // standardizedFileURL can strip /private only from existing paths, so using
    // it for containment makes the same URL change identity after materializing.
    static func relativeComponents(of url:URL,in storage:URL) -> [String]? {
        guard url.isFileURL,storage.isFileURL,url.host==storage.host else {return nil}
        let base=storage.pathComponents,parts=url.pathComponents
        guard !parts.contains("."),!parts.contains(".."),parts.starts(with:base) else {return nil}
        return Array(parts.dropFirst(base.count))
    }
    static func sameLocation(_ first:URL,_ second:URL) -> Bool {
        first.isFileURL && second.isFileURL && first.host==second.host && first.pathComponents==second.pathComponents
    }
}
