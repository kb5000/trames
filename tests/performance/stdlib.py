#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""One alternating ARM64 batch: integer/string loops plus real Vim/Python."""
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
import time
ROOT=Path(__file__).resolve().parents[2]
loader=importlib.util.spec_from_file_location('nonjit',ROOT/'tests/performance/nonjit.py')
nonjit=importlib.util.module_from_spec(loader);loader.loader.exec_module(nonjit)
def digest(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def metrics(stderr):
    lines=stderr.decode().splitlines();assert len(lines)==2,stderr[-4000:]
    native,runtime=[json.loads(s.split(': ',1)[1]) for s in lines]
    return dict(native=native,runtime=runtime)
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline',type=Path,required=True);p.add_argument('--baseline-commit',required=True)
    p.add_argument('--vim-store',type=Path,required=True);p.add_argument('--python-store',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    current=ROOT/'target/aarch64-unknown-linux-musl/release/trames-run';libc=ROOT/'target/musl-hybrid/libc.so';fixture=ROOT/'tests/fixtures/bin/musl-stdlib'
    qemu=os.getenv('QEMU_AARCH64','qemu-aarch64');rows=[]
    choices=[('previous-pure',a.baseline,'pure'),('current-pure',current,'pure'),('current-off',current,'off')]
    with tempfile.TemporaryDirectory(prefix='trames-stdlib-bench-') as directory:
        tmp=Path(directory)
        for name in ('strtoull-4096','strings-8191x256','vim-default','python-imports'):
            modes={}
            for iteration in range(3):
                ordered=choices[iteration:]+choices[:iteration]
                for mode,binary,enabled in ordered:
                    args=[qemu,'-cpu','max',str(binary),'--network','off','--musl-libc',str(libc),'--accelerator',enabled,'--metrics','--max-instructions','100000000000','--env','HOME=/root','--env','TERM=xterm-256color']
                    if name in ('vim-default','python-imports'):
                        store=tmp/'store';source=a.vim_store if name=='vim-default' else a.python_store;shutil.copytree(source,store)
                        args+=['--storage',str(store)]
                    else:
                        args+=['--memory','--rootfs',str(ROOT/'target/alpine-download/alpine-minirootfs-3.22.6-aarch64.tar.gz'),'--exec-file','/bin/bench='+str(fixture)]
                    if name=='vim-default':
                        args+=['--pty','--guest','/usr/bin/vim','/tmp/performance-empty.txt']
                        seconds,profile=nonjit.vim(args,metrics)
                        endpoint='process spawn to first cursor visibility after normal Vim startup; terminal queries answered; then normal :qa!'
                    else:
                        if name=='python-imports':args+=['--guest','/usr/bin/python3','-c',nonjit.PYTHON];wanted=b'python: 42\n'
                        elif name=='strtoull-4096':args+=['--guest','/bin/bench','bench'];wanted=b'stdlib-bench-ok\n'
                        else:args+=['--guest','/bin/bench','strings-bench'];wanted=b'strings-bench-ok\n'
                        start=time.monotonic();r=subprocess.run(args,capture_output=True,timeout=300);elapsed=time.monotonic()-start
                        assert (r.returncode,r.stdout)==(0,wanted),(name,mode,r.returncode,r.stdout[-2000:],r.stderr[-4000:]);profile=metrics(r.stderr)
                        seconds=elapsed if name=='python-imports' else profile['runtime']['seconds']
                        endpoint='process spawn to successful exit including final sync' if name=='python-imports' else 'guest execution including loader; host rootfs import excluded'
                    assert profile['native']['faults']==0 and profile['native']['rejected']==0,profile
                    modes.setdefault(mode,{'samples':[]})['samples'].append(dict(seconds=seconds,**profile))
                    print(name,mode,iteration+1,round(seconds,4),flush=True)
                    if name in ('vim-default','python-imports'):shutil.rmtree(store)
            for data in modes.values():data['median_seconds']=statistics.median(s['seconds'] for s in data['samples'])
            rows.append(dict(workload=name,endpoint=endpoint,modes=modes,previous_current_speedup=modes['previous-pure']['median_seconds']/modes['current-pure']['median_seconds'],off_on_speedup=modes['current-off']['median_seconds']/modes['current-pure']['median_seconds']))
    report=dict(scope='Actual ARM64 runtime under pinned QEMU on shared x86_64 Linux; three alternating samples; identical full libc and program/store per mode; warm host caches. Previous published runtime advertises floating but no new integer/string entries. No physical iPad timing or general speedup claim.',host=platform.platform(),baseline_commit=a.baseline_commit,previous_runtime_sha256=digest(a.baseline),current_runtime_sha256=digest(current),libc_sha256=digest(libc),fixture_sha256=digest(fixture),vim_manifest_sha256=digest(a.vim_store/'manifest'),python_manifest_sha256=digest(a.python_store/'manifest'),python_source_sha256=hashlib.sha256(nonjit.PYTHON.encode()).hexdigest(),rows=rows)
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n')
    print([(r['workload'],round(r['previous_current_speedup'],3)) for r in rows],flush=True)
if __name__=='__main__':main()
