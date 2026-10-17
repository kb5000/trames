// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
import CTrames

public struct LocalWaylandWindow: Decodable, Identifiable, Sendable, Equatable {
    public let id: UInt64
    public let connection: UInt64
    public let title: String
    public let app_id: String
    public let width: UInt32
    public let height: UInt32
    public let revision: UInt64
    public let mode: String
    public let bounds: [Int32]
    /// Pixel revisions are consumed directly by the display link, without
    /// rebuilding the SwiftUI workspace on every rendered guest frame.
    public func hasSameLayout(as other: Self) -> Bool {
        id == other.id && connection == other.connection && title == other.title
            && app_id == other.app_id && width == other.width && height == other.height
            && mode == other.mode && bounds == other.bounds
    }
}
public struct LocalWaylandText: Decodable, Sendable, Equatable {
    public let version: UInt32
    public let id: UInt64
    public let epoch: UInt64
    public let surface: UInt64
    public let window: UInt64
    public let serial: UInt32
    public let text: String
    public let cursor: UInt32
    public let anchor: UInt32
    public let caret: [Int32]
    public let hint: UInt32
    public let purpose: UInt32
}
public struct LocalWaylandStatus: Decodable, Sendable {
    public let revision: UInt64
    public let windows: [LocalWaylandWindow]
    public let text: LocalWaylandText?
}
/// A native capability owns all image backing until final GPU completion, even
/// if the guest closes its FD, destroys a surface, or the VM session shuts down.
public final class LocalWaylandFrame: @unchecked Sendable {
    private let lock = NSLock()
    private var handle: UInt64
    public let info: TramesWaylandFrameInfo
    public let images: [TramesWaylandImageInfo]
    init(handle: UInt64) throws {
        self.handle = handle
        var info = TramesWaylandFrameInfo()
        guard trames_wayland_frame_info(handle, &info) == 0 else {
            _ = trames_wayland_frame_release(handle, 0, 0)
            self.handle = 0
            throw LocalSession.lastError()
        }
        self.info = info
        var images: [TramesWaylandImageInfo] = []
        for index in 0..<info.images {
            var image = TramesWaylandImageInfo()
            guard trames_wayland_frame_image(handle, index, &image) == 0 else {
                _ = trames_wayland_frame_release(handle, 0, 0)
                self.handle = 0
                throw LocalSession.lastError()
            }
            images.append(image)
        }
        self.images = images
    }
    deinit { release(presented: false) }
    public func release(presented: Bool) {
        lock.lock(); defer { lock.unlock() }
        if handle != 0 {
            let milliseconds = UInt32(truncatingIfNeeded: UInt64(ProcessInfo.processInfo.systemUptime * 1000))
            _ = trames_wayland_frame_release(handle, presented ? 1 : 0, milliseconds)
            handle = 0
        }
    }
}
extension LocalSession {
    public func waylandStatus() throws -> LocalWaylandStatus {
        let h = try currentHandle()
        var data = Data(count: 65536)
        let count = data.withUnsafeMutableBytes {
            trames_session_wayland_status(h, $0.bindMemory(to: UInt8.self).baseAddress, UInt32($0.count))
        }
        guard count >= 0 else { throw Self.lastError() }
        data.count = Int(count)
        return try JSONDecoder().decode(LocalWaylandStatus.self, from: data)
    }
    public func waylandCommand(_ command: [String:Any]) throws {
        let data = try JSONSerialization.data(withJSONObject: command)
        guard data.count <= 65536 else { throw TramesError.message("Wayland command exceeds 64 KiB") }
        let h = try currentHandle()
        let result = data.withUnsafeBytes {
            trames_session_wayland_command(h, $0.bindMemory(to: UInt8.self).baseAddress, UInt32($0.count))
        }
        if result < 0 { throw Self.lastError() }
    }
    public func waylandFrame(window: UInt64) throws -> LocalWaylandFrame? {
        let id = trames_session_wayland_frame_acquire(try currentHandle(), window)
        return id == 0 ? nil : try LocalWaylandFrame(handle: id)
    }
}
