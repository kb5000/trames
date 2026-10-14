#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""One paired batch: allocator hot/cold transitions and unchanged Vim/Python."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('nonjit',ROOT/'tests/performance/nonjit.py')
nonjit=importlib.util.module_from_spec(spec);spec.loader.exec_module(nonjit)
digest=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def metrics(stderr):
    lines=stderr.decode().splitlines();assert len(lines)==2,stderr[-4000:]
    native,runtime=[json.loads(line.split(': ',1)[1]) for line in lines]
    assert not native['faults'] and not native['rejected'],native
    return dict(native=native,runtime=runtime)
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline',type=Path,required=True);p.add_argument('--baseline-commit',required=True)
    p.add_argument('--vim-store',type=Path,required=True);p.add_argument('--python-store',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    current=ROOT/'target/release/trames-run';libc=ROOT/'target/musl-hybrid/libc.so';fixture=ROOT/'tests/fixtures/bin/musl-allocator'
    rows=[]
    with tempfile.TemporaryDirectory(prefix='trames-allocator-bench-') as directory:
        for name,cpus in [('allocator',1),('allocator',4),('vim-default',1),('python-imports',1)]:
            modes={}
            for sample in range(3):
                choices=[('baseline',a.baseline),('current',current)]
                if sample%2:choices.reverse()
                for mode,binary in choices:
                    command=[str(binary),'--network','off','--cpus',str(cpus),'--musl-libc',str(libc),'--accelerator','pure','--metrics','--max-instructions','100000000000','--env','HOME=/root','--env','TERM=xterm-256color']
                    if mode=='current':command+=['--native-allocator']
                    if name=='allocator':
                        command+=['--memory','--rootfs',str(ROOT/'target/alpine-download/alpine-minirootfs-3.22.6-aarch64.tar.gz'),'--exec-file','/bin/allocator='+str(fixture),'--guest','/bin/allocator','bench','96']
                    else:
                        store=Path(directory)/'store';shutil.copytree(a.vim_store if name=='vim-default' else a.python_store,store)
                        command+=['--storage',str(store)]
                    if name=='vim-default':
                        command+=['--pty','--guest','/usr/bin/vim','/tmp/performance-empty.txt'];seconds,profile=nonjit.vim(command,metrics)
                    else:
                        if name=='python-imports':command+=['--guest','/usr/bin/python3','-c',nonjit.PYTHON]
                        result=subprocess.run(command,capture_output=True,timeout=300)
                        expected=b'musl-allocator-ok\n' if name=='allocator' else b'python: 42\n'
                        assert (result.returncode,result.stdout)==(0,expected),(name,mode,result.returncode,result.stdout[-1000:],result.stderr[-4000:])
                        profile=metrics(result.stderr);seconds=profile['runtime']['seconds']
                    if mode=='baseline':assert not profile['native'].get('allocator_calls',0),profile
                    else:assert profile['native']['allocator_allocations']>0,profile
                    modes.setdefault(mode,[]).append(dict(seconds=seconds,**profile))
                    print(name,cpus,sample,mode,round(seconds,6),flush=True)
                    if name!='allocator':shutil.rmtree(store)
            medians={mode:statistics.median(x['seconds'] for x in samples) for mode,samples in modes.items()}
            rows.append(dict(workload=name,cpus=cpus,samples=modes,median_seconds=medians,speedup=medians['baseline']/medians['current']))
    report=dict(baseline_commit=a.baseline_commit,baseline_sha256=digest(a.baseline),current_sha256=digest(current),libc_sha256=digest(libc),fixture_sha256=digest(fixture),host=platform.platform(),host_arch=platform.machine(),cpu_affinity=len(os.sched_getaffinity(0)),cpu_quota=Path('/sys/fs/cgroup/cpu.max').read_text().strip(),rows=rows,limits='Three alternating samples, identical guest ELF/libc/stores on both runtimes. x86_64 development interpreter; no physical iPad or NEON timing claim. Allocator benchmark uses four threads, 96 rounds of 192 allocations/thread, cross-thread frees and sampled payload checks. Vim measures spawn to normal first cursor visibility; others use runtime guest execution seconds, excluding rootfs import.')
    a.output.write_text(json.dumps(report,indent=2)+'\n')
    print([(row['workload'],row['cpus'],round(row['speedup'],3)) for row in rows],flush=True)
if __name__=='__main__':main()
