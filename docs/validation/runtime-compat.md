# Linux compatibility follow-up before full musl integration

Development source after 0.9.0/build 10, 2026-10-12. This addresses the observed
`sigaltstack` / `membarrier` / `ioprio_get` ENOSYS and `prlimit64` EOPNOTSUPP logs.
The immutable 0.9.0 IPA predates this work.

| Interface | Implemented behavior | Remaining boundary |
| --- | --- | --- |
| sigaltstack (132) | AArch64 stack_t, register/query/disable, actual SA_ONSTACK signal frames, nesting, SS_AUTODISARM, ucontext restore, fork inheritance and exec/CLONE_VM reset | 2048-byte ABI minimum; delivery needs space for the 4688-byte signal frame. No SVE/SME context |
| membarrier (283) | QUERY, GLOBAL, REGISTER_PRIVATE_EXPEDITED, PRIVATE_EXPEDITED and GET_REGISTRATIONS; host sequentially consistent fence at cooperative scheduling boundaries | One virtual CPU; no SYNC_CORE/RSEQ modes or CPU-target flags |
| ioprio_get (31) | Existing virtual task/current task query, CLASS_NONE/data 0, ESRCH for absent task | No ioprio_set, group/user queries or host I/O scheduling control |
| get/setrlimit, prlimit64 | RLIMIT_FSIZE soft/hard values independent of disk quota; hard-limit permissions, overlapping old/new pointers, thread sharing/fork copying/exec persistence | Current-process operations; other resource changes retain selected fixed limits |
| Regular-file write/truncate | Partial write at soft limit; EFBIG plus guest SIGXFSZ at/beyond limit, growth rejection, shrink permitted; pipes/devices unaffected | Disk quota still separately returns ENOSPC; no host process resource changes |
| dmesg / kmsg | Unsupported syscall, CPU fault and rejected NativeCall messages include bounded, escaped `comm` | Current task name, not historical argv; prctl(PR_SET_NAME) may change it |

The reported `membarrier` argument `10` is hexadecimal 0x10, the private expedited
registration command. Registration is inherited by fork, shared by CLONE_VM and
cleared by exec. The process name is obtained from executable basename or the
existing PR_SET_NAME state, for example `comm="probe-task"`.

Real static musl ARM64 fixtures pass against QEMU 10.0.13 in both memory and
persistent stores. They cover nested handlers on actual alternate-stack addresses,
EPERM while active, disable/query, file limits, partial writes, pwrite/ftruncate,
SIGXFSZ, unaffected pipes, fork/threads and actual child exec. QEMU user omits
SS_AUTODISARM, so that case is separately compared with the native Linux kernel
and executed on ARM64 Trames. Oracle-side ARM exec is skipped without host binfmt;
Trames executes the child ELF. Existing fault/context, mmap and BusyBox dmesg
regressions remain part of validation; the log fixture checks PR_SET_NAME output.
Core unit tests also exercise output-pointer preflight without state changes,
old/new overlap and unprivileged hard-limit rejection.

Reproduce after building pinned musl and the runtime:

```sh
bash scripts/build-runtime-compat-fixtures.sh
TRAMES_RUNNER=tests/differential/arm-runner.sh python3 tests/integration/runtime-compat.py
bash scripts/build-compatibility-log-fixtures.sh
TRAMES_RUNNER=tests/differential/arm-runner.sh python3 tests/integration/compatibility-log.py
```

This increases the numbered Linux 6.12 inventory to 164 implemented subsets,
152 missing and two obsolete entries. It does not establish complete Linux ABI
conformance or native acceleration of the complete musl library. The next full
hybrid musl build preserves the ordinary libc/loader filenames and retains
original guest implementations for functions not yet adapted to NativeCall.

Complete [Linux CI](https://github.com/kb5000/trames/actions/runs/38267059628)
and [Apple CI](https://github.com/kb5000/trames/actions/runs/38267059618) passed on
runtime commit `8911abc58268b80080a0e24908634bce8b5b010a`; detailed artifact and
seven-test simulator evidence is recorded in [NativeCall validation](accelerator-v1.md).
