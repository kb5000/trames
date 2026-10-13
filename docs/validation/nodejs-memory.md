# Node.js and interpreter memory follow-up

Development source after baseline `63e219098fea285a64720a821970cf4e029d8526`.
The released 0.10.0/build 11 is unchanged. One cached interpreter, no host JIT.
[Implementation and limits](../architecture/nodejs-memory.md).

The current local checks pass signed Alpine Node.js 22.23.2 and npm 11.6.4 on
the development-host executor, using the complete hybrid musl at its standard
path. The fixture checks process.title, repeated V8 arithmetic, Intl/URL/JSON,
asynchronous files and crypto, compression, workers, child processes, localhost
lookup, HTTP server/fetch and virtual egress. Offline npm installs a local
dependency, writes a lockfile and loads it through ordinary CommonJS. A separate
runtime reopens the persisted Node/npm data. CI runs this workflow on a fresh store.

The extended mmap fixture independently compares 4 GiB address reservation,
non-residency, permission SIGSEGV/SEGV_ACCERR, partial commits/removal, fixed
replacement and reserved mremap against QEMU. Actual ARM64 Trames execution
passes both memory and disk variants. Linux may apply a mapped prefix before a
failed mprotect across a hole; the comparison restores that prefix before reuse.
Trames preflights this case, keeping a failed commit atomic.

Rust tests cover failed memory/metadata-budget commits, fork COW, CLONE_VM,
range splitting and nonblocking standard-stream backpressure. ARM64 differentials
cover integer/load-store/structure operations and ordered accesses; the broader
existing musl and Linux regression suite runs in CI.

The manual performance harness is `tests/performance/cpu-memory.py`. It preserves
the preceding release binary and compares three alternating old/new ARM64 runs
under pinned QEMU. Its tight memory loop verifies its result and equal retirement;
ordinary Vim and Python start from fresh copies of identical stores. There is no
timing assertion, no benchmark in routine CI and no physical iPad timing claim.
Final raw measurements and exact-source CI evidence are recorded by the subsequent
validation update.

```sh
TRAMES_TEST_MUSL="$PWD/target/musl-hybrid/libc.so" python3 tests/integration/nodejs.py
python3 tests/performance/cpu-memory.py \
  --baseline /path/to/preserved/trames-run \
  --baseline-commit 63e219098fea285a64720a821970cf4e029d8526 \
  --vim-store /path/to/vim-store --python-store /path/to/python-store \
  --output /tmp/cpu-memory.json
```

Node/npm packages remain unmodified signed Alpine inputs; this is functional
coverage of the fixture, not universal npm-package or Node native-addon support.
Shared/file PROT_NONE, general demand paging, shared-proportional RSS, independent
virtual TCP keepalive retry timing and host JIT remain outside this change.
