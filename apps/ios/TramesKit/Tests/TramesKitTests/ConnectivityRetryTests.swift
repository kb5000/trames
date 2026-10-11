// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
@testable import TramesKit
final class ConnectivityRetryTests: XCTestCase {
    private actor Probe {
        var calls=0
        let failures:[URLError.Code]
        init(_ failures:[URLError.Code]) {self.failures=failures}
        func next() throws -> Int {
            calls+=1
            if calls<=failures.count {throw URLError(failures[calls-1])}
            return calls
        }
    }
    func testPermissionBecomesAvailableAndBootstrapReadAutomaticallyRecovers() async throws {
        let probe=Probe([.notConnectedToInternet,.dnsLookupFailed])
        let result=try await ConnectivityRetry.perform(operation:{try await probe.next()},delay:{_ in})
        XCTAssertEqual(result,3)
    }
    func testOfflineRetriesAreBoundedAndPermanentFailuresAreNotRetried() async {
        for (codes,expected) in [([URLError.Code](repeating:.notConnectedToInternet,count:4),3),([.badServerResponse],1)] {
            let probe=Probe(codes)
            do {_ = try await ConnectivityRetry.perform(operation:{try await probe.next()},delay:{_ in});XCTFail("Expected failure")}
            catch {}
            let calls=await probe.calls; XCTAssertEqual(calls,expected)
        }
    }
    func testCancellationDuringPermissionWaitPreventsAnotherRead() async {
        let probe=Probe([.notConnectedToInternet])
        do {_ = try await ConnectivityRetry.perform(operation:{try await probe.next()},delay:{_ in throw CancellationError()});XCTFail("Expected cancellation")}
        catch {XCTAssertTrue(error is CancellationError)}
        let calls=await probe.calls;XCTAssertEqual(calls,1)
    }
}
