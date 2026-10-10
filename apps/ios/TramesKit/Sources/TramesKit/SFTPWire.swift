// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
import NIO
import NIOSSH
/// SFTP v3 file-copy subset. Exactly one request is outstanding; all frames are
/// capped at 64 KiB, file chunks at 32 KiB, opaque handles at 256 bytes. No path
/// is interpreted as a native filename. The parent actor serializes transfers.
final class SFTPWire: @unchecked Sendable {
    let channel: Channel
    private var iterator: AsyncThrowingStream<ByteBuffer,Error>.Iterator
    private var requestID: UInt32 = 0
    private init(channel: Channel, stream: AsyncThrowingStream<ByteBuffer,Error>) {
        self.channel = channel; iterator = stream.makeAsyncIterator()
    }
    static func open(parent: Channel) async throws -> SFTPWire {
        let (stream, continuation) = AsyncThrowingStream<ByteBuffer,Error>.makeStream(bufferingPolicy: .bufferingOldest(4))
        // Opening an unanswered channel is also bounded. Before a child
        // exists, cancelling/expiry must close the containing SSH connection.
        let openingTimeout = parent.eventLoop.scheduleTask(in: .seconds(20)) { parent.close(promise: nil) }
        let channel: Channel
        do {
            channel = try await withTaskCancellationHandler(operation: {
                try await parent.eventLoop.flatSubmit {
                    let promise = parent.eventLoop.makePromise(of: Channel.self)
                    do {
                        let ssh = try parent.pipeline.syncOperations.handler(type: NIOSSHHandler.self)
                        ssh.createChannel(promise) { child,_ in
                            child.pipeline.addHandler(SFTPPackets(continuation: continuation))
                        }
                    } catch { promise.fail(error) }
                    return promise.futureResult
                }.get()
            }, onCancel: { parent.close(promise: nil) })
            openingTimeout.cancel()
        } catch { openingTimeout.cancel(); throw error }
        let wire = SFTPWire(channel: channel, stream: stream)
        let setupTimeout = channel.eventLoop.scheduleTask(in: .seconds(20)) { channel.close(promise: nil) }
        defer { setupTimeout.cancel() }
        return try await withTaskCancellationHandler(operation: {
        do {
            try Task.checkCancellation()
            try await channel.triggerUserOutboundEvent(SSHChannelRequestEvent.SubsystemRequest(subsystem: "sftp", wantReply: true))
            var hello = ByteBuffer(); hello.writeInteger(UInt8(1)); hello.writeInteger(UInt32(3))
            try await wire.send(hello)
            var reply = try await wire.receive()
            guard reply.readInteger(as: UInt8.self) == 2, reply.readInteger(as: UInt32.self) == 3 else { throw TramesError.message("SFTP v3 is required") }
            return wire
        } catch { wire.close(); throw error }
        }, onCancel: { wire.close() })
    }
    func close() { channel.close(promise: nil) }
    private func send(_ payload: ByteBuffer) async throws {
        var frame = ByteBuffer(); frame.writeInteger(UInt32(payload.readableBytes)); frame.writeImmutableBuffer(payload)
        let channel = channel
        let timeout = channel.eventLoop.scheduleTask(in: .seconds(20)) { channel.close(promise: nil) }
        defer { timeout.cancel() }
        try await withTaskCancellationHandler(operation: {
            try Task.checkCancellation()
            try await channel.writeAndFlush(SSHChannelData(type: .channel, data: .byteBuffer(frame)))
        }, onCancel: { channel.close(promise: nil) })
    }
    private func receive() async throws -> ByteBuffer {
        let channel = channel
        let timeout = channel.eventLoop.scheduleTask(in: .seconds(20)) { channel.close(promise: nil) }
        defer { timeout.cancel() }
        return try await withTaskCancellationHandler(operation: {
            guard let frame = try await iterator.next() else { throw TramesError.message("SFTP channel closed or request timed out") }
            return frame
        }, onCancel: { channel.close(promise: nil) })
    }
    private func request(_ type: UInt8, payload: ByteBuffer) async throws -> (UInt8,ByteBuffer) {
        guard requestID < UInt32.max else { throw TramesError.message("SFTP request limit reached") }
        requestID += 1
        let channel = channel
        let timeout = channel.eventLoop.scheduleTask(in: .seconds(20)) { channel.close(promise: nil) }
        defer { timeout.cancel() }
        var frame = ByteBuffer(); frame.writeInteger(type); frame.writeInteger(requestID); frame.writeImmutableBuffer(payload)
        try await send(frame)
        var reply = try await receive()
        guard let response = reply.readInteger(as: UInt8.self), reply.readInteger(as: UInt32.self) == requestID else { throw TramesError.message("SFTP reply identifier mismatch") }
        return (response,reply)
    }
    private func string(_ bytes: Data, into buffer: inout ByteBuffer) { buffer.writeInteger(UInt32(bytes.count)); buffer.writeBytes(bytes) }
    private func readString(_ buffer: inout ByteBuffer, maximum: Int) throws -> Data {
        guard let size = buffer.readInteger(as: UInt32.self), size <= maximum, let bytes = buffer.readBytes(length: Int(size)) else { throw TramesError.message("Invalid SFTP string length") }
        return Data(bytes)
    }
    private func status(_ type: UInt8, _ payload: ByteBuffer, allowEOF: Bool = false) throws -> Bool {
        var payload = payload
        guard type == 101, let code = payload.readInteger(as: UInt32.self) else { throw TramesError.message("Unexpected SFTP reply") }
        if code == 0 { return false }
        if allowEOF && code == 1 { return true }
        throw TramesError.message("SFTP server returned status \(code)")
    }
    func openFile(path: String, flags: UInt32) async throws -> Data {
        var payload = ByteBuffer(); string(Data(path.utf8), into: &payload); payload.writeInteger(flags)
        payload.writeInteger(UInt32(4)); payload.writeInteger(UInt32(0o600)) // SSH_FILEXFER_ATTR_PERMISSIONS
        let (type, response) = try await request(3, payload: payload)
        if type != 102 { _ = try status(type,response); throw TramesError.message("SFTP handle required") }
        var bytes = response
        let handle = try readString(&bytes, maximum: 256)
        guard !handle.isEmpty, bytes.readableBytes == 0 else { throw TramesError.message("Invalid SFTP handle") }; return handle
    }
    func closeFile(handle: Data) async throws {
        var payload = ByteBuffer(); string(handle,into: &payload)
        let (type, bytes) = try await request(4, payload: payload); _ = try status(type,bytes)
    }
    func remove(path: String) async throws {
        var payload = ByteBuffer(); string(Data(path.utf8),into: &payload)
        let (type, bytes) = try await request(13, payload: payload); _ = try status(type,bytes)
    }
    func write(handle: Data, bytes: Data, offset: UInt64) async throws {
        var payload = ByteBuffer(); string(handle,into: &payload); payload.writeInteger(offset); string(bytes,into: &payload)
        let (type, reply) = try await request(6, payload: payload); _ = try status(type,reply)
    }
    func read(handle: Data, offset: UInt64, count: UInt32) async throws -> Data {
        var payload = ByteBuffer(); string(handle,into: &payload); payload.writeInteger(offset); payload.writeInteger(count)
        let (type, response) = try await request(5, payload: payload)
        if type == 101 {
            guard try status(type,response,allowEOF: true) else { throw TramesError.message("SFTP read returned status without data") }; return Data()
        }
        guard type == 103 else { throw TramesError.message("SFTP data required") }
        var bytes = response; let result = try readString(&bytes,maximum: Int(count))
        guard !result.isEmpty, bytes.readableBytes == 0 else { throw TramesError.message("Invalid SFTP read data") }; return result
    }
}
final class SFTPPackets: ChannelInboundHandler {
    typealias InboundIn = SSHChannelData
    let continuation: AsyncThrowingStream<ByteBuffer,Error>.Continuation
    private var pending = ByteBuffer()
    init(continuation: AsyncThrowingStream<ByteBuffer,Error>.Continuation) { self.continuation = continuation }
    func channelRead(context: ChannelHandlerContext, data: NIOAny) {
        let event = unwrapInboundIn(data)
        guard case .byteBuffer(let bytes) = event.data, event.type == .channel, bytes.readableBytes <= 65536 else { fail(context,"Invalid SFTP channel packet"); return }
        pending.writeImmutableBuffer(bytes)
        while let size = pending.getInteger(at: pending.readerIndex, as: UInt32.self) {
            guard size > 0, size <= 65536 else { fail(context,"SFTP frame exceeds 64 KiB"); return }
            if pending.readableBytes < Int(size)+4 { break }
            pending.moveReaderIndex(forwardBy: 4)
            guard let frame = pending.readSlice(length: Int(size)) else { fail(context,"Invalid SFTP frame"); return }
            switch continuation.yield(frame) {
            case .enqueued: break
            case .dropped: fail(context,"SFTP reply queue overflow"); return
            case .terminated: context.close(promise: nil); return
            @unknown default: context.close(promise: nil); return
            }
        }
        pending.discardReadBytes()
    }
    private func fail(_ context: ChannelHandlerContext, _ error: String) { continuation.finish(throwing: TramesError.message(error)); context.close(promise: nil) }
    func channelInactive(context: ChannelHandlerContext) { continuation.finish(); context.fireChannelInactive() }
    func errorCaught(context: ChannelHandlerContext, error: Error) { continuation.finish(throwing: error); context.close(promise: nil) }
    func userInboundEventTriggered(context: ChannelHandlerContext, event: Any) {
        if event is ChannelFailureEvent { fail(context,"SSH server refused SFTP") }
        else { context.fireUserInboundEventTriggered(event) }
    }
}
