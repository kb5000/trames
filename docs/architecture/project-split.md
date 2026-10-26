# Three-repository layout

The source projects are now independently maintained:

| Repository | Ownership |
| --- | --- |
| [trames-runtime](https://github.com/kb5000/trames-runtime) | Execution contracts, guest memory/reclamation, ELF, Linux ABI/image loading, the process/thread kernel and scheduler, signals/futexes/PTYs, VFS/network/storage, distribution loading, decoder-independent native/Vulkan services and Wayland protocol services. |
| [trames-ipad](https://github.com/kb5000/trames-ipad) | iPad application, ARM64 interpreter/compute implementation and its thin Core composition; guest acceleration profiles remain product options. Existing repository identity/history, issues, PRs and releases are retained by the rename. |
| [trames-mac](https://github.com/kb5000/trames-mac) | Hypervisor transport and Apple Silicon product entry points. |

Each product has its own workspace and lockfile. Its `runtime/` Git submodule
pins an exact runtime commit; `runtime-version.json` records that commit and the
extracted content digest. Product Cargo dependencies resolve to this in-tree
checkout. No absolute peer-checkout path, interpreter dependency in common code,
or private signing credential is required to build the common runtime.

The complete kernel/executor migration is reviewed on `codex/shared-process-kernel`
until it is merged. Add `--branch codex/shared-process-kernel` when cloning a
product for this checkpoint.

```sh
git clone --recurse-submodules https://github.com/kb5000/trames-ipad.git
git clone --recurse-submodules https://github.com/kb5000/trames-mac.git
git clone https://github.com/kb5000/trames-runtime.git
```

After switching product revisions, run `git submodule update --init`.
`scripts/check-execution-boundaries.py` verifies the clean exact runtime pin and
the composition graph. Update common code in the runtime repository, commit and
validate it, then explicitly update each product's submodule and recorded pin.
Do not patch separate product copies of Linux semantics.

Related Git history and authors were preserved with filtered exports. The
runtime's 237 extracted crate/vendor files were compared byte-for-byte with
source commit `d035516132cb32c41635cc4fa9d97923b08718d7`. Repository/workspace
manifests, lockfiles, build paths and CI wiring change at extraction; guest
implementations do not. `SOURCE.json` in each new repository records provenance.

Common tests run in their standalone workspace; product integration remains in
the product CI. Product checkout steps initialize submodules, and local CI input
hashes include runtime sources, manifests and the pin so runtime changes cannot
reuse stale product results. Full local CI precedes pushes. Historical validation
records retain their original paths/source hashes.

`trames-session` owns the existing process table, fork/clone/exec/wait lifecycle,
thread groups, signals, futexes, terminal/job control and worker scheduler. The
iPad `trames-core` facade exports `Runtime<trames_cpu::Cpu>`; the Mac product uses
`Runtime<trames_hvf::kernel::Cpu>`. Both instantiate the same production kernel.
`trames-engine::Cpu` separates movable task state from a thread-affine `Engine`.
The interpreter retains its existing decoder/deferred flags and memory hot path;
HVF retains its vCPU on the creator host thread. Linux process semantics are not
reimplemented in the Mac frontend. Backend HWCAP/feature names apply to initial
load, every exec and procfs. Hardware execution reports instruction retirement
as unavailable and rejects finite exact instruction budgets.

The original internal process/worker tests compile from the shared source as the
iPad `kernel-conformance` integration target. Run `cargo test -p trames-core
--test kernel-conformance --locked`, or include `--tests` in a `--lib` selection.
Standalone common-workspace builds never depend on an interpreter or HVF.
Native/Vulkan crates moved with the kernel because they depend on guest memory
and values, not on a decoder; product option flags still control their profiles.

The Mac frontend is terminal-first. It supports external programs, pipelines,
threads, script exec, foreground/background jobs and persistent transactional VFS
storage through the existing common implementations. Hardware execution currently
uses one host execution thread; shared hardware mapping providers, simultaneous
HVF vCPUs, a native Wayland window presenter, live-anonymous host-pressure paging
and physical iPad release acceptance remain separate gates.
