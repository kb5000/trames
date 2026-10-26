#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Optional callback benchmark: original fallback, native, full-frame native."""
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
RUNNER=os.getenv('TRAMES_RUNNER',str(ROOT/'tests/differential/arm-runner.sh'))
BASELINE=os.getenv('TRAMES_CALLBACK_BASELINE_RUNNER')
LIBC=ROOT/'target/musl-hybrid/libc.so'
ARCHIVE=ROOT/'target/alpine-download/alpine-minirootfs-3.22.6-aarch64.tar.gz'
rows=[]
with tempfile.TemporaryDirectory(prefix='trames-callback-bench-') as folder:
    data=Path(folder)/'input';data.write_text(''.join(f'{i:05d} '+('payload'*8)+'\n' for i in range(2048,0,-1)))
    expected=b''.join(sorted(data.read_bytes().splitlines(keepends=True)))
    workloads=[('qsort-integers-2048',['--exec-file','/bin/bench='+str(ROOT/'tests/fixtures/bin/musl-callbacks'),'--guest','/bin/bench','2048'],b'callbacks-bench-ok\n'),('alpine-sort-n-2048',['--file','/root/input='+str(data),'--guest','/bin/busybox','sort','-n','/root/input'],expected)]
    for name,args,wanted in workloads:
        modes={}
        choices=[('off',RUNNER,'off'),('on',RUNNER,'pure')]+([('previous-full-frame',BASELINE,'pure')] if BASELINE else [])
        # Alternate modes to reduce mode-order bias on this shared cloud host.
        for iteration in range(3):
            for mode,runner,enabled in choices[iteration%len(choices):]+choices[:iteration%len(choices)]:
                r=subprocess.run([runner,'--memory','--rootfs',str(ARCHIVE),'--musl-libc',str(LIBC),'--accelerator',enabled,'--metrics','--max-instructions','2000000000',*args],capture_output=True,timeout=180)
                assert (r.returncode,r.stdout)==(0,wanted),(name,mode,r)
                native,runtime=[json.loads(line.split(': ',1)[1]) for line in r.stderr.decode().splitlines()]
                assert native['callback_calls']>0 if enabled=='pure' else native['calls']==0,native
                modes.setdefault(mode,{'samples':[]})['samples'].append({'native':native,'runtime':runtime})
        for mode in modes.values():mode['median_seconds']=statistics.median(s['runtime']['seconds'] for s in mode['samples'])
        row={'workload':name,'modes':modes,'off_on_speedup':modes['off']['median_seconds']/modes['on']['median_seconds']}
        if BASELINE:row['continuation_speedup']=modes['previous-full-frame']['median_seconds']/modes['on']['median_seconds']
        rows.append(row);print(name,round(row['off_on_speedup'],2),'times',flush=True)
report={'description':'Three alternating samples per mode; same full musl and guest program. Median guest wall time includes startup, excludes host rootfs import. Actual ARM64 runtime under QEMU on x86_64; no device/general performance claim. Previous-full-frame binary already reuses immediate-return instruction quanta, but transfers the whole sorting frame at every comparison. It predates the experimental bsearch routing flag, which neither measured workload uses.','host':platform.platform(),'runner':RUNNER,'libc_sha256':hashlib.sha256(LIBC.read_bytes()).hexdigest(),'runtime_sha256':hashlib.sha256((ROOT/'target/aarch64-unknown-linux-musl/release/trames-run').read_bytes()).hexdigest(),'baseline_runtime_sha256':hashlib.sha256(Path(os.environ['TRAMES_CALLBACK_BASELINE_BINARY']).read_bytes()).hexdigest() if os.getenv('TRAMES_CALLBACK_BASELINE_BINARY') else None,'source_sha256':{name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in ['runtime/crates/trames-accel/src/callbacks.rs','runtime/crates/trames-session/src/process.rs','accelerator/musl/callbacks.c','accelerator/musl/callbacks.h','tests/fixtures/musl-callbacks.c']},'rows':rows}
(ROOT/'target/musl-hybrid/callback-benchmark.json').write_text(json.dumps(report,indent=2)+'\n')
