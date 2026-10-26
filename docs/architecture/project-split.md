# Three-repository layout

The source projects are now independently maintained:

| Repository | Ownership |
| --- | --- |
| [trames-runtime](https://github.com/kb5000/trames-runtime) | Execution contracts, guest memory/reclamation, ELF, Linux ABI/image loading, shared task signals/affinity, VFS/network/storage, distribution loading and Wayland protocol services. |
| [trames-ipad](https://github.com/kb5000/trames-ipad) | iPad application, interpreter, interpreter accelerators, Vulkan integration and current Core process/executor composition. Existing repository identity/history, issues, PRs and releases are retained by the rename. |
| [trames-mac](https://github.com/kb5000/trames-mac) | Hypervisor transport and Apple Silicon product entry points. |

Each product has its own workspace and lockfile. Its `runtime/` Git submodule
pins an exact runtime commit; `runtime-version.json` records that commit and the
extracted content digest. Product Cargo dependencies resolve to this in-tree
checkout. No absolute peer-checkout path, interpreter dependency in common code,
or private signing credential is required to build the common runtime.

The iPad migration is reviewed on `codex/project-split` until it is merged.
Add `--branch codex/project-split` when cloning that product for this checkpoint.

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

Physical repository separation does not imply Mac/iPad feature parity. The Mac
runner remains single-process. The full process table, clone/exec image lifecycle,
executor scheduling, external signal/wait routing and shared hardware mapping
providers still need integration behind the shared boundary. Live-anonymous
pressure paging and physical iPad release acceptance remain separate gates.
