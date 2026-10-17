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
    mutating func constrain(to viewport: CGSize) {
        size.width = min(max(240, size.width), max(1, viewport.width))
        size.height = min(max(200, size.height), max(1, viewport.height))
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
    private var previous: [WorkspaceItemID: Placement] = [:]
    var frames: [WorkspaceItemID: DisplayWindowGeometry] = [:]
    private(set) var selected: WorkspaceItemID?
    private(set) var focused: WorkspaceItemID?
    func placement(_ id: WorkspaceItemID) -> Placement? {
        if tabs.contains(id) { return .tabbed }
        if floating.contains(id) { return .floating }
        if minimized.contains(id) { return .minimized }
        return nil
    }
    mutating func insert(_ id: WorkspaceItemID, as placement: Placement) {
        guard self.placement(id) == nil else { return }
        switch placement {
        case .tabbed: tabs.append(id); select(id)
        case .floating: floating.append(id); focus(id)
        case .minimized: minimized.append(id)
        }
    }
    mutating func select(_ id: WorkspaceItemID) {
        guard tabs.contains(id) else { return }
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
        removePlacement(id); floating.append(id)
        if let origin { var frame = frames[id] ?? DisplayWindowGeometry(); frame.origin = origin; frames[id] = frame }
        focus(id)
    }
    mutating func minimize(_ id: WorkspaceItemID) {
        guard let place = placement(id), place != .minimized else { return }
        previous[id] = place
        removePlacement(id); minimized.append(id)
        if focused == id { focused = selected ?? floating.last }
    }
    mutating func restore(_ id: WorkspaceItemID) {
        guard minimized.contains(id) else { return }
        let place = previous.removeValue(forKey: id) ?? .floating
        if place == .tabbed { dock(id) } else { detach(id) }
    }
    mutating func close(_ id: WorkspaceItemID) {
        removePlacement(id); frames.removeValue(forKey: id); previous.removeValue(forKey: id)
        if focused == id { focused = selected ?? floating.last }
    }
    mutating func closeSession(_ session: UUID) {
        for id in tabs + floating + minimized where id.session == session { close(id) }
    }
    private mutating func removePlacement(_ id: WorkspaceItemID) {
        if let index = tabs.firstIndex(of: id) {
            tabs.remove(at: index)
            if selected == id { selected = tabs.isEmpty ? nil : tabs[min(index, tabs.count - 1)] }
        }
        floating.removeAll { $0 == id }; minimized.removeAll { $0 == id }
    }
}
