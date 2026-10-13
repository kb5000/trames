#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""One old/new batch for checked memory, NativeCall and unchanged Vim/Python."""
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
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
nonjit=load('nonjit',ROOT/'tests/performance/nonjit.py')
def digest(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def metrics(stderr):
    lines=stderr.decode().splitlines();assert len(lines)==2,stderr[-4000:]
    native,runtime=[json.loads(s.split(': ',1)[1]) for s in lines]
    return dict(native=native,runtime=runtime)
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline',type=Path,required=True);parser.add_argument('--baseline-commit',required=True)
    parser.add_argument('--baseline-probe',type=Path,required=True);parser.add_argument('--candidate-probe',type=Path,required=True)
    parser.add_argument('--vim-store',type=Path,required=True);parser.add_argument('--python-store',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True);a=parser.parse_args()
    qemu=os.getenv('QEMU_AARCH64','qemu-aarch64');current=ROOT/'target/aarch64-unknown-linux-musl/release/trames-run';libc=ROOT/'target/musl-hybrid/libc.so';fixture=ROOT/'tests/fixtures/bin/musl-stdlib'
    rows=[];probes={}
    for sample in range(3):
        choices=[('previous',a.baseline_probe),('current',a.candidate_probe)];choices=choices if sample%2==0 else choices[::-1]
        for mode,binary in choices:
            result=subprocess.run([qemu,'-cpu','max',str(binary)],capture_output=True,timeout=120)
            assert result.returncode==0,(result.returncode,result.stderr[-4000:])
            for line in result.stdout.splitlines():
                item=json.loads(line);probes.setdefault(item.pop('workload'),{}).setdefault(mode,[]).append(item)
    for name,modes in probes.items():
        for mode,values in modes.items():assert len(values)==3 and len({(r['allocations'],r['allocated_bytes']) for r in values})==1,(name,mode,values)
        speedup=statistics.median(s['seconds'] for s in modes['previous'])/statistics.median(s['seconds'] for s in modes['current'])
        print('memory-probe',name,round(speedup,3),'times; allocations',modes['previous'][0]['allocations'],'->',modes['current'][0]['allocations'],flush=True)
    with tempfile.TemporaryDirectory(prefix='trames-boundary-bench-') as directory:
        tmp=Path(directory)
        for name in ('strtoull-4096','strings-8191x256','vim-default','python-imports'):
            modes={}
            for sample in range(3):
                choices=[('previous-pure',a.baseline),('current-pure',current)];choices=choices if sample%2==0 else choices[::-1]
                for mode,binary in choices:
                    command=[qemu,'-cpu','max',str(binary),'--network','off','--musl-libc',str(libc),'--accelerator','pure','--metrics','--max-instructions','100000000000','--env','HOME=/root','--env','TERM=xterm-256color','--env','PYTHONHASHSEED=0']
                    if name in ('vim-default','python-imports'):
                        store=tmp/'store';shutil.copytree(a.vim_store if name=='vim-default' else a.python_store,store);command+=['--storage',str(store)]
                    else:command+=['--memory','--rootfs',str(ROOT/'target/alpine-download/alpine-minirootfs-3.22.6-aarch64.tar.gz'),'--exec-file','/bin/bench='+str(fixture)]
                    if name=='vim-default':
                        command+=['--pty','--guest','/usr/bin/vim','/tmp/performance-empty.txt'];seconds,profile=nonjit.vim(command,metrics);endpoint='process spawn to first visible cursor; ordinary startup, terminal replies, then :qa!'
                    else:
                        if name=='python-imports':command+=['--guest','/usr/bin/python3','-c',nonjit.PYTHON];expected=b'python: 42\n'
                        elif name=='strtoull-4096':command+=['--guest','/bin/bench','bench'];expected=b'stdlib-bench-ok\n'
                        else:command+=['--guest','/bin/bench','strings-bench'];expected=b'strings-bench-ok\n'
                        start=time.monotonic();r=subprocess.run(command,capture_output=True,timeout=300);elapsed=time.monotonic()-start;assert (r.returncode,r.stdout)==(0,expected),(name,mode,r.returncode,r.stdout[-2000:],r.stderr[-4000:]);profile=metrics(r.stderr)
                        seconds=elapsed if name=='python-imports' else profile['runtime']['seconds'];endpoint='process spawn through exit/final sync' if name=='python-imports' else 'guest execution including loader; host rootfs import excluded'
                    assert profile['native']['faults']==0 and profile['native']['rejected']==0,profile
                    if mode=='current-pure':assert profile['native']['validation_hits']>0,profile
                    modes.setdefault(mode,{'samples':[]})['samples'].append(dict(seconds=seconds,**profile));print(name,mode,sample+1,round(seconds,4),flush=True)
                    if name in ('vim-default','python-imports'):shutil.rmtree(store)
            for mode in modes.values():mode['median_seconds']=statistics.median(s['seconds'] for s in mode['samples'])
            # Guest programs/ABI/control paths must not change merely for speed.
            if name in ('strtoull-4096','strings-8191x256'):
                assert {s['runtime']['instructions'] for s in modes['previous-pure']['samples']}=={s['runtime']['instructions'] for s in modes['current-pure']['samples']},name
            rows.append(dict(workload=name,endpoint=endpoint,modes=modes,speedup=modes['previous-pure']['median_seconds']/modes['current-pure']['median_seconds']))
    report=dict(scope='Actual ARM64 runtime under pinned QEMU on shared x86_64 Linux; three alternating old/current pure samples. Identical complete libc, ELF and freshly copied stores. Warm host caches; fixed PYTHONHASHSEED=0 in both modes; application clocks/entropy/PTY delivery can still change retirement. No physical iPad or general speedup claim. Memory probes use identical source/lockfile and an isolated allocation-counting process; their runtime includes checks/assertions.',baseline_commit=a.baseline_commit,host=platform.platform(),previous_runtime_sha256=digest(a.baseline),current_runtime_sha256=digest(current),libc_sha256=digest(libc),fixture_sha256=digest(fixture),previous_probe_sha256=digest(a.baseline_probe),current_probe_sha256=digest(a.candidate_probe),probe_source_sha256=digest(ROOT/'tests/performance/boundary-cost/src/main.rs'),probe_lockfile_sha256=digest(ROOT/'tests/performance/boundary-cost/Cargo.lock'),vim_manifest_sha256=digest(a.vim_store/'manifest'),python_manifest_sha256=digest(a.python_store/'manifest'),python_source_sha256=hashlib.sha256(nonjit.PYTHON.encode()).hexdigest(),memory_probes=probes,rows=rows)
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n');print([(r['workload'],round(r['speedup'],3)) for r in rows],flush=True)
if __name__=='__main__':main()
