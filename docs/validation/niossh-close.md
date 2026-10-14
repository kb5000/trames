# Buffered reads during local SSH channel close

Tracker: [Issue #9](https://github.com/kb5000/trames/issues/9).
The first 0.14 Apple build and its unchanged-source retry both trapped in
`ChildChannelStateMachine.swift:432` during the real OpenSSH test:
[run 38563169660](https://github.com/kb5000/trames/actions/runs/38563169660).
Those failures remain evidence; they are not passed off as a fix.

A deterministic regression opens an embedded child with auto-read disabled,
queues an entire receive window, locally closes it before peer acknowledgement,
and then drains the queued read. The unmodified pinned NIOSSH 0.3.4 source exits
with signal 4 on Linux at the same window-adjust precondition. The test observes
all final bytes, exactly the open/close outbound messages, and successful close
completion after peer acknowledgement.

The [vendored minimal patch](../../vendor/README.md) permits window adjustment
only in active, half-closed or quiescent states. It still delivers final buffered
data. It preserves normal window adjustments and does not suppress the state
machine trap or retry tests until they pass.

Local Swift 6.0.3 validation passes all 55 tests, including 40 upstream/multiplexer
cases and the real OpenSSH 1 MiB upload/download, cancellation, separate-shell
usability and EOF/close workflow. Linux's unprivileged OpenSSH test disables PTY;
the Mac job remains the reference for PTY and resize. All eight remaining remote
Swift dependency pins match the preceding lockfile. Original Apache-2.0/MIT
licenses and per-file upstream hashes are retained.

The exact-source `6596b68` [Mac run](https://github.com/kb5000/trames/actions/runs/38569532812)
passed its native OpenSSH PTY/resize and SFTP step on the first attempt on
2026-10-14 (13:12:03–13:14:02 UTC). The complete App/simulator job was still running
when this record was written. Physical transport acceptance remains in
[Issue #8](https://github.com/kb5000/trames/issues/8).

Meanwhile, the automatic publisher had already released the preceding
`29464a8` source as [0.14.0/build 15](release-0.14.md), after both of its complete
workflows passed. Its immutable tag and IPA do not contain this later SSH patch.
The patch remains on main for a subsequent grouped update.
