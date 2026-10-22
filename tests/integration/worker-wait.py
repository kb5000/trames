#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real musl timerfd/ppoll waits alongside fixed native work; structural checks."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner',type=Path,required=True)
p.add_argument('--oracle',type=Path,required=True)
p.add_argument('--baseline',type=Path)
p.add_argument('--fixture',type=Path,default=Path('tests/fixtures/bin/worker-wait'))
p.add_argument('--output',type=Path,required=True)
p.add_argument('--cpus',type=int,nargs='+',choices=(1,2,4),default=(1,2,4))
p.add_argument('--modes',nargs='+',choices=('timer','epoll','ppoll','timeout'),default=('timer','epoll','ppoll','timeout'))
p.add_argument('--rounds',type=int,default=32)
a=p.parse_args()
assert 1<=a.rounds<=1024
a.output.mkdir(parents=True,exist_ok=False)
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
report=dict(scope='Real static ARM64 musl waits plus rounds*16 native 1 MiB fills. '
                  'Host C semantic output oracle; structural counts, no timing claim.',
            runner_sha256=sha(a.runner),oracle_sha256=sha(a.oracle),fixture_sha256=sha(a.fixture),
            baseline_sha256=sha(a.baseline) if a.baseline else None,passed=False,rows=[])
try:
    for mode in a.modes:
        for affinity in ('pinned','spread'):
            tail=[mode,affinity,str(a.rounds)]
            expected=subprocess.run([str(a.oracle.resolve()),*tail],capture_output=True,timeout=30)
            assert expected.returncode==0 and not expected.stderr,expected
            (a.output/f'{mode}-{affinity}-oracle.stdout').write_bytes(expected.stdout)
            for cpus in a.cpus:
                versions=[('after',a.runner)]
                if a.baseline: versions.insert(0,('before',a.baseline))
                for version,binary in versions:
                    label=f'{mode}-{affinity}-{cpus}-{version}'
                    cmd=[str(binary.resolve()),'--memory','--network','off','--cpus',str(cpus),
                         '--accelerator','pure','--metrics','--max-instructions','1000000000',str(a.fixture.resolve()),*tail]
                    r=subprocess.run(cmd,capture_output=True,timeout=60)
                    (a.output/(label+'.stdout')).write_bytes(r.stdout)
                    (a.output/(label+'.stderr')).write_bytes(r.stderr)
                    assert (r.returncode,r.stdout)==(0,expected.stdout),(label,r.returncode,r.stdout,r.stderr[-6000:])
                    values=dict(line.split(': ',1) for line in r.stderr.decode().splitlines() if line.startswith('trames '))
                    runtime=json.loads(values['trames metrics']); native=json.loads(values['trames native metrics'])
                    assert native['function_calls'][2]==a.rounds*16 and native['bytes_written']==a.rounds*16*(1<<20),native
                    assert native['faults']==native['rejected']==native['fallbacks']==0,native
                    if version=='after' and cpus>1:
                        key='worker_poll_syscalls' if mode=='timeout' else 'worker_timer_syscalls'
                        assert runtime[key]>0 and native['worker_calls']>0,runtime
                        if mode=='ppoll': assert runtime['worker_poll_syscalls']>0,runtime
                        assert runtime['service_requests']==runtime['serviced_syscalls']==0,runtime
                        if affinity=='pinned': assert runtime['peak_parallel_cpus']==1,runtime
                    report['rows'].append(dict(mode=mode,affinity=affinity,cpus=cpus,version=version,runtime=runtime,native=native))
                    print(label,'PASS','epochs',runtime['parallel_rounds'],'timer',runtime.get('worker_timer_syscalls',0),
                          'poll',runtime.get('worker_poll_syscalls',0),flush=True)
    report['passed']=True
finally:
    (a.output/'results.json').write_text(json.dumps(report,indent=2)+'\n')
