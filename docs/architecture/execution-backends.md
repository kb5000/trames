# Shared Linux execution backends

The project is split into the common [trames-runtime](https://github.com/kb5000/trames-runtime),
this iPad interpreter product, and [trames-mac](https://github.com/kb5000/trames-mac).
[Project boundaries and validation](project-split.md) describes the package graph,
exact runtime pins and normal development workflow.

`trames-session` instantiates one production Linux process kernel over the cold
`trames-engine::Cpu` interface. The iPad backend retains the interpreter's
register/decode state. The Mac backend retains a thread-affine HVF engine on each
execution worker. Task state can move without moving hardware vCPUs. Linux
fork/clone/exec, signals, futexes, VFS and terminal semantics remain common.

Hardware CPUs now share canonical frozen-View physical pages. A scoped host
borrow revokes hardware access to its physical granule before Rust references
are exposed; affected guest tasks park and release their execution slots.
Ordinary hardware instructions keep the ARM atomicity/memory-order model.
The common [region-memory contract](https://github.com/kb5000/trames-runtime/blob/main/docs/architecture/region-memory.md)
records ownership, quotas, COW and the controlled storage-transfer boundary.

Mac live dirty memory uses preserved file-backed page-out/page-in, an optional
hardware working-set target and supported macOS pressure notifications. See the
Mac [hardware execution and paging design](https://github.com/kb5000/trames-mac/blob/main/docs/architecture/execution-backends.md)
for VM/ASID ownership, pressure handling and repeatable hardware acceptance.
Hardware instruction counts remain unavailable. Working-set targets do not cap
all host RSS, and current frozen-view fork preparation can copy private pages
before their actual first write.

Native Mac Wayland presentation and external raw GPU-buffer exports remain
separate integration work. The iPad product continues to use its interpreter and
existing platform/file-provider integration; it acquires no HVF dependency.
