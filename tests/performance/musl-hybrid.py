#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Optional single baseline: same complete musl, acceleration off/on."""
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
RUNNER=os.getenv('TRAMES_RUNNER',str(ROOT/'target/release/trames-run'))
LIBC=ROOT/'target/musl-hybrid/libc.so'
ARCHIVE=Path(os.getenv('TRAMES_ALPINE_CACHE',str(ROOT/'target/alpine-download')))/'alpine-minirootfs-3.22.6-aarch64.tar.gz'
rows=[]
with tempfile.TemporaryDirectory(prefix='trames-musl-bench-') as directory:
    data=Path(directory)/'sort-input'
    data.write_text(''.join(f'{i:05d} '+('payload'*8)+'\n' for i in range(2048,0,-1)))
    expected=b''.join(sorted(data.read_bytes().splitlines(keepends=True)))
    workloads=[('copy-32',['--exec-file','/bin/bench='+str(ROOT/'tests/fixtures/bin/accelerator-full-musl'),'--guest','/bin/bench','32','copy'],b'accelerator-bench-ok\n'),('copy-65536',['--exec-file','/bin/bench='+str(ROOT/'tests/fixtures/bin/accelerator-full-musl'),'--guest','/bin/bench','65536','copy'],b'accelerator-bench-ok\n'),('alpine-sort-2048',['--file','/root/sort-input='+str(data),'--guest','/bin/busybox','sort','-n','/root/sort-input'],expected)]
    for name,args,wanted in workloads:
        modes={}
        for enabled in (False,True):
            samples=[]
            for _ in range(3):
                r=subprocess.run([RUNNER,'--memory','--rootfs',str(ARCHIVE),'--musl-libc',str(LIBC),'--metrics','--max-instructions','2000000000','--accelerator','pure' if enabled else 'off',*args],capture_output=True,timeout=180)
                assert (r.returncode,r.stdout)==(0,wanted),r
                lines=r.stderr.decode().splitlines();assert len(lines)==2,r.stderr
                samples.append({'native':json.loads(lines[0].split(': ',1)[1]),'runtime':json.loads(lines[1].split(': ',1)[1])})
            modes[str(enabled)]={'samples':samples,'median_seconds':statistics.median(s['runtime']['seconds'] for s in samples)}
        rows.append({'workload':name,'modes':modes,'speedup':modes['False']['median_seconds']/modes['True']['median_seconds']})
        print(name,round(rows[-1]['speedup'],2),'times',flush=True)
report={'host':platform.platform(),'runner':RUNNER,'description':'Three runs per mode; median guest runtime includes loader/startup, excludes host rootfs import. ARM64 runtime itself runs under QEMU on this x86_64 host. No physical-iPad or general application performance claim. Same full musl ELF and guest program in both modes; not a comparison against stock Alpine libc.','source_sha256':{n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in ['accelerator/guest/pure.c','accelerator/musl/init.c','scripts/build-musl-hybrid.sh','crates/trames-distro/src/lib.rs','crates/trames-cli/src/main.rs']},'libc_sha256':hashlib.sha256(LIBC.read_bytes()).hexdigest(),'runner_binary_sha256':hashlib.sha256((ROOT/'target/aarch64-unknown-linux-musl/release/trames-run').read_bytes()).hexdigest(),'fixture_sha256':hashlib.sha256((ROOT/'tests/fixtures/bin/accelerator-full-musl').read_bytes()).hexdigest(),'rows':rows}
(ROOT/'target/musl-hybrid/benchmark.json').write_text(json.dumps(report,indent=2)+'\n')
