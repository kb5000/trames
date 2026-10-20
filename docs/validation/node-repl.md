# Node REPL regression investigation

The owner reports `local runtime panicked` after entering text such as `111` in
Node. This is a caught host Rust panic, rather than a Node exception. The exact
iPad trigger and panic location are still unknown.

`tests/integration/node-repl.py` exercises unmodified Alpine Node 22.23.2 through
the production App FFI, hybrid musl, Wayland and multiple-terminal settings. It
uses 4 GiB aggregate/per-process memory and disabled networking. Both terminal 1
and a newly opened terminal 2 evaluate whole-line `111`, delayed character input
with a one-second eager-preview interval, arithmetic and an object. It requires
actual result lines, subsequent REPL prompts, `.exit` and a complete shell output
marker; input echo alone cannot pass. Logs and current status survive failure.

The Linux x86_64/4 KiB host passed all eight checks and returned to both shells
using the unchanged runtime from `95830f6`. This does not establish iPad acceptance
or close the regression. [Results](node-repl/linux-results.json) and
[checks](node-repl/linux-checks.log) retain the runtime/libc hashes.

The manual `Node REPL compatibility` workflow builds the current hybrid musl
from pinned original sources, then tests the same production FFI on Apple ARM64
with an asserted 16 KiB host page size. It deliberately avoids rerunning unchanged
CPU, VFS, Linux, network, graphics and UI suites, and creates no IPA.
Apple results are pending. No production runtime change is included here.
