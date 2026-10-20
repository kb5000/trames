# Firefox serial exclusive-monitor progress

The one-CPU diagnostic uses original Alpine ARM64 Firefox 142, default sandbox
and Wayland proxy, 4 GiB aggregate/per-process memory, networking/GPU off, and
only the explicit diagnostic preference `layout.frame_rate=1`. Host affinity is
restricted before the production session opens, so Runtime selects one CPU.
This does not change the product default.

After startup work, the parent main thread repeatedly samples inside libxul
`0x3136794..0x31367c8` (parent load bias `0x335b4000`). The original instructions
load an exclusive value, spill the desired value to `[sp,#64]`, and attempt
`STLXR`. It repeatedly fails and retries. The plain serial Memory implementation
used the whole VM's revision as a private-page reservation token: any unrelated
stack write invalidated it. This is an overly conservative monitor that prevents
progress on this real compiler-generated sequence. It is distinct from the
16 KiB host-group relocation panic fixed for Node.

The fix promotes a private page to the existing SharedPage byte/reservation domain
on its first exclusive load. Unique heap/mapped storage transfers ownership;
true fork COW detaches, and an uncommitted zero page remains uncharged until a
write. Serial CPU, native writes and parallel CPUs then use the same existing
16-byte reservation versions. Ordinary pages get no new per-store monitor map,
version table or global lock. Permissions, acquire/release fences, ABA checks,
retained views and worker epoch rules remain. Custom live backing providers that
do not expose shared storage retain their existing conservative fallback.

`CpuMemory::exclusive_read` now takes `&mut self` to permit this one-time storage
promotion. The worker algorithm is unchanged; its signature and an allocator
fixture forwarding method follow the interface. A previously immutable test
binding becomes mutable.

The CPU regression executes the original `LDAXR X9,[X23]`, `STR X11,[SP,#64]`,
`STLXR W10,X11,[X23]` encoding. It [fails before](firefox-exclusive/serial-cpu-before.log)
and passes after the change. Additional memory regressions cover different
16-byte granules in the same page, ABA stores, native writes, fork COW,
unmap/remap identity and demand-zero accounting. A mapping relocation can cause
a bounded retry while establishing a new physical reservation identity.

Linux passes 92 Memory, 43 CPU, 25 Accelerator and 87 Core tests (one manual
benchmark remains ignored), plus strict Clippy of these affected modules.
[Checks](firefox-exclusive/linux-tests.log) and
[Clippy](firefox-exclusive/linux-clippy.log) are retained. These are scoped checks;
unchanged unrelated network, Vulkan and UI acceptance is reused.

The original Firefox page and Apple ARM64/16 KiB checks of this fix are pending.
No browser-usability or speedup claim is made from the unit regression alone.
