# Pinned SSH transport sources

`swift-nio-ssh` retains Wellz26/swift-nio-ssh 0.3.4 at
`b93961a2988607a756cbc21a811f406f27aa9ab6`, with its Apache-2.0 license.
`citadel` retains orlandos-nl/Citadel 0.12.1 at
`ae8562f895de06ccb86fdb1cbb65fd99c8976e12`, with its MIT license.
Each `UPSTREAM.json` records original per-file SHA-256 values. These are the
production sources and package manifests required to build the selected products;
the fixed upstream repositories retain their remaining development files.

The NIOSSH changes add `ChildChannelStateMachine.canSendWindowAdjust` and check
it when draining buffered reads. `didClose` becomes true only after complete
closure, while a locally closing channel is still active awaiting its peer's
acknowledgement. Reading queued data in that interval previously generated a
window-adjust message that its own state machine rejected with a fatal trap.
The guard follows exactly the states accepted by `sendChannelWindowAdjust`.
Buffered data still reaches the pipeline; active and half-closed flow control
retain upstream behavior.

Citadel changes only its package dependency to the same local NIOSSH source.
Keeping both local avoids a root/local versus transitive/remote package-identity
conflict. The other eight remote Swift dependency pins remain unchanged.

The Apache-licensed upstream multiplexer tests are retained in
`apps/ios/TramesKit/Tests/NIOSSHRegressionTests`, with a local-close/queued-read
regression. It traps deterministically with the unchanged upstream source and
passes after this patch. [Evidence](../docs/validation/niossh-close.md).
