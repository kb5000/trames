#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""One optional alternating batch against the preceding published ARM64 runtime."""
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
CURRENT=ROOT/'target/aarch64-unknown-linux-musl/release/trames-run'
BASELINE=Path(os.environ['TRAMES_NUMERIC_BASELINE_BINARY'])
QEMU=os.getenv('QEMU_AARCH64','qemu-aarch64')
LIBC=ROOT/'target/musl-hybrid/libc.so'
ARCHIVE=ROOT/'target/alpine-download/alpine-minirootfs-3.22.6-aarch64.tar.gz'
rows=[]
with tempfile.TemporaryDirectory(prefix='trames-numeric-bench-') as folder:
    data=Path(folder)/'input';data.write_text(''.join(f'{i:05d} '+('payload'*8)+'\n' for i in range(2048,0,-1)))
    expected=b''.join(sorted(data.read_bytes().splitlines(keepends=True)))
    workloads=[('strtod-4096',['--exec-file','/bin/bench='+str(ROOT/'tests/fixtures/bin/musl-numeric'),'--guest','/bin/bench','bench'],b'numeric-bench-ok\n'),('qsort-integers-2048',['--exec-file','/bin/bench='+str(ROOT/'tests/fixtures/bin/musl-callbacks'),'--guest','/bin/bench','2048'],b'callbacks-bench-ok\n'),('alpine-sort-n-2048',['--file','/root/input='+str(data),'--guest','/bin/busybox','sort','-n','/root/input'],expected)]
    for name,args,wanted in workloads:
        modes={};choices=[('previous-pure',BASELINE,'pure'),('current-pure',CURRENT,'pure'),('current-off',CURRENT,'off')]
        for iteration in range(3):
            for mode,binary,enabled in choices[iteration:]+choices[:iteration]:
                r=subprocess.run([QEMU,'-cpu','max',str(binary),'--memory','--rootfs',str(ARCHIVE),'--musl-libc',str(LIBC),'--accelerator',enabled,'--metrics','--max-instructions','2000000000',*args],capture_output=True,timeout=180)
                assert (r.returncode,r.stdout)==(0,wanted),(name,mode,r.returncode,r.stderr[-4000:])
                native,runtime=[json.loads(line.split(': ',1)[1]) for line in r.stderr.decode().splitlines()]
                if mode=='current-pure' and name!='qsort-integers-2048':assert native['numeric_conversions']>1000,native
                modes.setdefault(mode,{'samples':[]})['samples'].append({'native':native,'runtime':runtime})
        for mode in modes.values():mode['median_seconds']=statistics.median(s['runtime']['seconds'] for s in mode['samples'])
        row={'workload':name,'modes':modes,'previous_current_speedup':modes['previous-pure']['median_seconds']/modes['current-pure']['median_seconds'],'off_on_speedup':modes['current-off']['median_seconds']/modes['current-pure']['median_seconds']}
        rows.append(row);print(name,round(row['previous_current_speedup'],2),'times',flush=True)
report={'description':'Three alternating samples per mode; identical full libc and guest program. Previous runtime comes from the preceding published source commit and lacks numeric auxv; it selects original musl conversion. Actual ARM64 runtime under QEMU on a shared x86_64 host. Times include guest loader/startup, exclude host rootfs import. No physical iPad or general whole-system gain is established.','baseline_commit':os.environ['TRAMES_NUMERIC_BASELINE_COMMIT'],'host':platform.platform(),'current_runtime_sha256':hashlib.sha256(CURRENT.read_bytes()).hexdigest(),'previous_runtime_sha256':hashlib.sha256(BASELINE.read_bytes()).hexdigest(),'libc_sha256':hashlib.sha256(LIBC.read_bytes()).hexdigest(),'fixture_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/'tests/fixtures/bin/musl-numeric',ROOT/'tests/fixtures/bin/musl-callbacks']},'rows':rows}
(ROOT/'target/musl-hybrid/numeric-benchmark.json').write_text(json.dumps(report,indent=2)+'\n')
