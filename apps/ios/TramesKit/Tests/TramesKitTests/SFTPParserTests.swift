// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
import NIO
import NIOEmbedded
import NIOSSH
@testable import TramesKit
final class SFTPParserTests: XCTestCase {
    func testFragmentedSFTPFrameAndHeader() async throws {
        let (stream,c) = AsyncThrowingStream<ByteBuffer,Error>.makeStream(bufferingPolicy: .bufferingOldest(4))
        let channel = EmbeddedChannel(handler: SFTPPackets(continuation: c))
        defer { _ = try? channel.finish() }
        var frame = ByteBuffer(); frame.writeInteger(UInt32(9)); frame.writeInteger(UInt8(101)); frame.writeInteger(UInt32(7)); frame.writeInteger(UInt32(0))
        for size in [2,3,8] {
            let part = frame.readSlice(length: size)!
            _ = try channel.writeInbound(SSHChannelData(type: .channel, data: .byteBuffer(part)))
        }
        var iterator = stream.makeAsyncIterator()
        var reply = try await iterator.next()!
        XCTAssertEqual(reply.readInteger(as: UInt8.self),101)
        XCTAssertEqual(reply.readInteger(as: UInt32.self),7)
        XCTAssertEqual(reply.readInteger(as: UInt32.self),0)
    }
    func testZeroAndOversizedFrameLengthsCloseTheChannel() async throws {
        for length: UInt32 in [0,65537,UInt32.max] {
            let (stream,c) = AsyncThrowingStream<ByteBuffer,Error>.makeStream(bufferingPolicy: .bufferingOldest(4))
            let channel = EmbeddedChannel(handler: SFTPPackets(continuation: c))
            var frame = ByteBuffer(); frame.writeInteger(length)
            _ = try channel.writeInbound(SSHChannelData(type: .channel, data: .byteBuffer(frame)))
            var iterator = stream.makeAsyncIterator()
            do { _ = try await iterator.next(); XCTFail("invalid frame accepted") } catch {}
            XCTAssertFalse(channel.isActive)
            _ = try? channel.finish()
        }
    }
}
