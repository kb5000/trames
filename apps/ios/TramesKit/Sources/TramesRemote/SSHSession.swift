// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
import TramesKit
import Citadel
import NIO
import NIOSSH
import Crypto
public enum SSHCredential {
    case password(String)
    case ed25519(String, passphrase: String?)
}
/// One actor owns the native connection and its PTY. No guest ABI sockets are used.
public actor SSHSession {
    private var writer: Channel?
    private var transport: Channel?
    private var started = false
    private var transferring = false
    private var closed = false
    private var columns = 80, rows = 24
    public init() {}
    public func run(host: SSHHost, credential: SSHCredential, knownHosts: KnownHosts,
                    allocatePTY: Bool = true,
                    onChallenge: @escaping @Sendable (HostKeyChallenge) -> Void,
                    output: @escaping @Sendable (Data) async throws -> Void) async throws {
        guard !closed, !started else { throw TramesError.message("SSH session already used") }
        let authentication: SSHAuthenticationMethod
        switch credential {
        case .password(let password): authentication = .passwordBased(username: host.user, password: password)
        case .ed25519(let text, let passphrase):
            let key = try Curve25519.Signing.PrivateKey(sshEd25519: text, decryptionKey: passphrase?.data(using: .utf8))
            authentication = .ed25519(username: host.user, privateKey: key)
        }
        started = true
        let configuration = SSHClientConfiguration(userAuthDelegate: authentication,
            serverAuthDelegate: HostValidator(knownHosts: knownHosts, endpoint: host.endpoint, onChallenge: onChallenge))
        let channel = try await ClientBootstrap(group: MultiThreadedEventLoopGroup.singleton)
            .channelInitializer { channel in channel.pipeline.addHandlers([
                NIOSSHHandler(role: .client(configuration), allocator: channel.allocator,
                    inboundChildChannelInitializer: { child,_ in child.close() }),
                ConnectionFailureHandler()
            ]) }
            .connectTimeout(.seconds(15)).connect(host: host.hostname, port: host.port).get()
        guard !closed, !Task.isCancelled else { try? await channel.close(); throw CancellationError() }
        transport = channel
        let deadline = channel.eventLoop.scheduleTask(in: .seconds(20)) { channel.close(promise: nil) }
        defer { deadline.cancel() }
        do {
            let (stream, continuation) = AsyncThrowingStream<Data,Error>.makeStream(bufferingPolicy: .bufferingOldest(64))
            let child = try await channel.eventLoop.flatSubmit {
                let promise = channel.eventLoop.makePromise(of: Channel.self)
                do {
                    let sshHandler = try channel.pipeline.syncOperations.handler(type: NIOSSHHandler.self)
                    sshHandler.createChannel(promise) { child,_ in
                        child.pipeline.addHandler(BoundedTerminalHandler(continuation: continuation))
                    }
                } catch { promise.fail(error) }
                return promise.futureResult
            }.get()
            if allocatePTY {
                try await child.triggerUserOutboundEvent(SSHChannelRequestEvent.PseudoTerminalRequest(wantReply: true, term: "xterm-256color",
                    terminalCharacterWidth: columns, terminalRowHeight: rows, terminalPixelWidth: 0, terminalPixelHeight: 0, terminalModes: .init([:])))
            }
            try await child.triggerUserOutboundEvent(SSHChannelRequestEvent.ShellRequest(wantReply: true))
            writer = child; deadline.cancel()
            try await resize(columns: columns, rows: rows)
            for try await bytes in stream {
                try Task.checkCancellation(); try await output(bytes)
            }
            writer = nil; transport = nil
            try? await child.close(); try? await channel.close()
        } catch {
            writer = nil; transport = nil; try? await channel.close(); throw error
        }
    }
    public func send(_ bytes: Data) async throws {
        guard bytes.count <= 65536, let writer, !closed else { throw TramesError.message("SSH terminal is not ready") }
        try await writer.writeAndFlush(SSHChannelData(type: .channel, data: .byteBuffer(ByteBuffer(bytes: bytes))))
    }
    public func resize(columns: Int, rows: Int) async throws {
        guard (1...4096).contains(columns), (1...4096).contains(rows) else { return }
        self.columns = columns; self.rows = rows
        try await writer?.triggerUserOutboundEvent(SSHChannelRequestEvent.WindowChangeRequest(
            terminalCharacterWidth: columns, terminalRowHeight: rows, terminalPixelWidth: 0, terminalPixelHeight: 0))
    }
    public func close() async {
        closed = true; writer = nil
        let channel = transport; transport = nil
        try? await channel?.close()
    }
    /// Bounded 32 KiB SFTP chunks. Upload is exclusive and download must target a
    /// new native staging file; neither direction silently overwrites files.
    public func transfer(remote: String, local: URL, upload: Bool) async throws {
        guard remote.utf8.count <= 4096, !remote.isEmpty, !remote.contains("\0"), let transport, writer != nil, !closed, !transferring else {
            throw TramesError.message("SFTP session is not ready, busy or path is invalid")
        }
        transferring = true; defer { transferring = false }
        let sftp = try await SFTPWire.open(parent: transport)
        try await withTaskCancellationHandler(operation: {
        do {
            if upload {
                let input = try FileHandle(forReadingFrom: local)
                defer { try? input.close() }
                let remoteFile = try await sftp.openFile(path: remote, flags: 2|8|32)
                do {
                    var offset: UInt64 = 0
                    while let bytes = try input.read(upToCount: 32768), !bytes.isEmpty {
                        try Task.checkCancellation()
                        try await sftp.write(handle: remoteFile, bytes: bytes, offset: offset)
                        offset += UInt64(bytes.count)
                    }
                    try await sftp.closeFile(handle: remoteFile)
                } catch { try? await sftp.closeFile(handle: remoteFile); try? await sftp.remove(path: remote); throw error }
            } else {
                guard !FileManager.default.fileExists(atPath: local.path), FileManager.default.createFile(atPath: local.path, contents: nil) else { throw TramesError.message("SFTP destination already exists or cannot be created") }
                let target = try FileHandle(forWritingTo: local)
                defer { try? target.close() }
                do {
                    let remoteFile = try await sftp.openFile(path: remote, flags: 1)
                    do {
                        var offset: UInt64 = 0
                        while true {
                            try Task.checkCancellation()
                            let buffer = try await sftp.read(handle: remoteFile, offset: offset, count: 32768)
                            if buffer.isEmpty { break }
                            try target.write(contentsOf: buffer); offset += UInt64(buffer.count)
                        }
                        try await sftp.closeFile(handle: remoteFile)
                    } catch { try? await sftp.closeFile(handle: remoteFile); throw error }
                } catch { try? FileManager.default.removeItem(at: local); throw error }
            }
            sftp.close()
        } catch { sftp.close(); throw error }
        }, onCancel: { sftp.close() })
    }
}

/// Own the PTY stream rather than Citadel's unbounded convenience stream. If the
/// native renderer falls behind, terminate explicitly instead of losing bytes.
private final class BoundedTerminalHandler: ChannelInboundHandler {
    typealias InboundIn = SSHChannelData
    let continuation: AsyncThrowingStream<Data,Error>.Continuation
    private var exitCode = 0
    init(continuation: AsyncThrowingStream<Data,Error>.Continuation) { self.continuation = continuation }
    func channelRead(context: ChannelHandlerContext, data: NIOAny) {
        let event = unwrapInboundIn(data)
        guard case .byteBuffer(let buffer) = event.data, buffer.readableBytes <= 65536 else {
            continuation.finish(throwing: TramesError.message("SSH terminal packet exceeds 64 KiB")); context.close(promise: nil); return
        }
        switch continuation.yield(Data(buffer.readableBytesView)) {
        case .enqueued: break
        case .dropped:
            continuation.finish(throwing: TramesError.message("SSH output exceeded the bounded renderer queue")); context.close(promise: nil)
        case .terminated: context.close(promise: nil)
        @unknown default: context.close(promise: nil)
        }
    }
    func userInboundEventTriggered(context: ChannelHandlerContext, event: Any) {
        if let status = event as? SSHChannelRequestEvent.ExitStatus { exitCode = status.exitStatus }
        else if event is ChannelFailureEvent {
            continuation.finish(throwing: TramesError.message("SSH server refused the terminal request")); context.close(promise: nil)
        } else { context.fireUserInboundEventTriggered(event) }
    }
    func channelInactive(context: ChannelHandlerContext) {
        if exitCode == 0 { continuation.finish() }
        else { continuation.finish(throwing: TramesError.message("SSH shell exited with status \(exitCode)")) }
        context.fireChannelInactive()
    }
    func errorCaught(context: ChannelHandlerContext, error: Error) { continuation.finish(throwing: error); context.close(promise: nil) }
}

private final class ConnectionFailureHandler: ChannelInboundHandler {
    typealias InboundIn = ByteBuffer
    func errorCaught(context: ChannelHandlerContext, error: Error) { context.close(promise: nil) }
}
