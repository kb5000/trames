// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation

/// Identity survives title changes, docking and native view replacement.
enum WorkspaceItemID: Hashable {
    case terminal(UUID)
    case wayland(UUID, UInt64)
    var session: UUID {
        switch self { case .terminal(let id), .wayland(let id, _): return id }
    }
    var token: String {
        switch self {
        case .terminal(let id): return "terminal-\(id.uuidString)"
        case .wayland(let id, let window): return "wayland-\(id.uuidString)-\(window)"
        }
    }
}
struct DisplayWindowGeometry: Equatable {
    var origin = CGPoint(x: 32, y: 90)
    var size = CGSize(width: 600, height: 490)
    mutating func constrain(to viewport: CGSize, minimum: CGSize = CGSize(width:240,height:200)) {
        size.width = min(max(minimum.width, size.width), max(1, viewport.width))
        size.height = min(max(minimum.height, size.height), max(1, viewport.height))
        origin.x = min(max(0, origin.x), max(0, viewport.width - size.width))
        origin.y = min(max(0, origin.y), max(0, viewport.height - size.height))
    }
}
/// Window placement is independent of the guest session that owns a surface.
/// A selected tab and keyboard focus are separate: a floating window can have
/// focus while Alpine remains the selected background tab.
struct WorkspaceTabs: Equatable {
    enum Placement: Equatable { case tabbed, floating, minimized }
    private(set) var tabs: [WorkspaceItemID] = []
    private(set) var floating: [WorkspaceItemID] = []
    private(set) var minimized: [WorkspaceItemID] = []
    var frames: [WorkspaceItemID: DisplayWindowGeometry] = [:]
    private(set) var viewport = CGSize(width:1024,height:768)
    private(set) var selected: WorkspaceItemID?
    private(set) var focused: WorkspaceItemID?
    func placement(_ id: WorkspaceItemID) -> Placement? {
        if minimized.contains(id) { return .minimized }
        if tabs.contains(id) { return .tabbed }
        if floating.contains(id) { return .floating }
        return nil
    }
    mutating func insert(_ id: WorkspaceItemID, as placement: Placement) {
        guard self.placement(id) == nil else { return }
        switch placement {
        case .tabbed: tabs.append(id); select(id)
        case .floating: positionNewWindow(id); floating.append(id); focus(id)
        case .minimized: tabs.append(id); minimized.append(id)
        }
    }
    mutating func select(_ id: WorkspaceItemID) {
        guard tabs.contains(id) else { return }
        minimized.removeAll { $0 == id }
        selected = id; focus(id)
    }
    mutating func focus(_ id: WorkspaceItemID) {
        guard let place = placement(id), place != .minimized else { return }
        focused = id
        if place == .floating { floating.removeAll { $0 == id }; floating.append(id) }
    }
    mutating func dock(_ id: WorkspaceItemID, before target: WorkspaceItemID? = nil) {
        guard placement(id) != nil else { return }
        let wasSelected = selected == id
        removePlacement(id)
        let index = target.flatMap { tabs.firstIndex(of: $0) } ?? tabs.count
        tabs.insert(id, at: index)
        selected = wasSelected ? id : selected
        select(id)
    }
    mutating func detach(_ id: WorkspaceItemID, at origin: CGPoint? = nil) {
        guard placement(id) != nil else { return }
        if origin == nil { positionNewWindow(id) }
        removePlacement(id); floating.append(id)
        if let origin { var frame = frames[id] ?? DisplayWindowGeometry(); frame.origin = origin; frames[id] = frame }
        focus(id)
    }
    mutating func updateViewport(_ size: CGSize) {
        guard size.width > 0, size.height > 0, size != viewport else { return }
        viewport=size
        for id in Array(frames.keys) { frames[id]?.constrain(to:size,minimum:CGSize(width:1,height:1)) }
    }
    /// Only automatic initial placement cascades. Explicit drag positions and
    /// saved user geometry survive subsequent docking/restoration.
    private mutating func positionNewWindow(_ id: WorkspaceItemID) {
        guard frames[id] == nil else { return }
        var frame=DisplayWindowGeometry();frame.constrain(to:viewport)
        let occupied=floating.compactMap { frames[$0]?.origin }
        let start=frame.origin
        let maxX=max(0,viewport.width-frame.size.width), maxY=max(0,viewport.height-frame.size.height)
        func isFree(_ origin:CGPoint) -> Bool {
            !occupied.contains { abs($0.x-origin.x)<14 && abs($0.y-origin.y)<14 }
        }
        // Try a diagonal cascade first, then search the remaining visible grid.
        for step in 0...occupied.count {
            let candidate=CGPoint(x:min(maxX,start.x+CGFloat(step)*28),y:min(maxY,start.y+CGFloat(step)*28))
            if isFree(candidate) { frame.origin=candidate;frames[id]=frame;return }
        }
        for y in stride(from:CGFloat(0),through:maxY,by:28) {
            for x in stride(from:CGFloat(0),through:maxX,by:28) {
                let candidate=CGPoint(x:x,y:y)
                if isFree(candidate) { frame.origin=candidate;frames[id]=frame;return }
            }
        }
        frames[id]=frame
    }
    /// Minimization docks an inactive tab. The tab itself is always the restore
    /// target, including windows minimized by an xdg_toplevel request.
    mutating func minimize(_ id: WorkspaceItemID) {
        guard let place = placement(id), place != .minimized else { return }
        let wasFocused = focused == id
        removePlacement(id); tabs.append(id); minimized.append(id)
        if wasFocused { focused = selected ?? floating.last }
    }
    mutating func restore(_ id: WorkspaceItemID) {
        guard minimized.contains(id) else { return }
        select(id)
    }
    mutating func close(_ id: WorkspaceItemID) {
        removePlacement(id); frames.removeValue(forKey: id)
        if focused == id { focused = selected ?? floating.last }
    }
    mutating func closeSession(_ session: UUID) {
        for id in tabs + floating where id.session == session { close(id) }
    }
    private mutating func removePlacement(_ id: WorkspaceItemID) {
        if let index = tabs.firstIndex(of: id) {
            tabs.remove(at: index)
            if selected == id {
                let available = tabs.filter { !minimized.contains($0) }
                selected = available.isEmpty ? nil : available[min(index, available.count - 1)]
            }
        }
        floating.removeAll { $0 == id }; minimized.removeAll { $0 == id }
    }
}
