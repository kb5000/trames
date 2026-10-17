# Multiple local terminals

The + menu creates another Alpine terminal in the same running Linux instance.
Each window has its own original BusyBox `ash`, guest PTY, session/foreground
process group, terminal dimensions, input/output queues and SwiftTerm view.
Files, processes, networking, memory budget, Wayland and the native Bridge belong
to one environment. `cd` and exported shell variables remain local to a shell.

The opt-in `multiple_terminals` embedding option retains one guest PID 1. It runs
the original BusyBox shell with monitor mode and reads a private inherited pipe.
The coordinator allocates PTY masters in a separate VFS descriptor table sharing
the existing filesystem and namespaces; only slave paths enter the init pipe.
Original BusyBox `setsid` starts a new session, and a shell opens that slave after
`setsid` before executing interactive `ash`. Monitor mode is necessary: ordinary
asynchronous shell commands inherit ignored SIGINT. Interrupted init reads are
retried, and children are reaped. No terminal master or control writer is inherited
by a user shell. There is no new guest daemon binary or external listening port.

The trusted host ABI uses monotonic terminal IDs, up to 32 live terminals and
64 KiB of input/output buffering per terminal. Open is asynchronous. Each PTY
has independent backpressure; a full `yes` output cannot stop another shell.
Input-generated signals and host resize requests reach only the controlling
foreground group. Normal PTY hangup semantics apply when a terminal closes;
other terminals and the environment stay alive. A cancelled open retains its
master until init has opened the queued slave, preventing PTY-number reuse from
starting an old request inside a new terminal. Closing the entire session stops
the Linux environment. The default single-terminal embedding behavior remains.

The iPad workspace retains the environment owner after its first terminal view
closes. Additional terminal views own only PTY IDs. Wayland windows keep the
owner's stable identity, and clipboard/Bridge, timezone, pause/resume and display
polling happen once per environment. All visible terminals can float, minimize,
dock and close independently. Closing all terminal views then opening another
reattaches to the live environment; resetting Linux closes every terminal first.

`tests/integration/multiple-terminals.py` exercises the production ABI with a
fresh original Alpine rootfs: three terminals, shared files/processes, private
cwd/environment, independent sizes, Ctrl+C, output backpressure, first-window
closure, shell exit, reopen and rapid cancellation before startup. A coordinator
unit checks SIGINT/SIGWINCH foreground/session scoping. The iPad simulator test
uses the real workspace, SwiftTerm and guest PTYs, shared clipboard after closing
the first view, pause/resume and reopening after all terminal views close.
