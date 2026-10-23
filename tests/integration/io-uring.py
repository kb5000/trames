#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real AArch64 io_uring ABI, mapped shared queues, tasks and host TCP."""
import argparse
import hashlib
import json
import resource
import re
from pathlib import Path
import socket
import subprocess
import threading
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner',type=Path,required=True)
p.add_argument('--fixture',type=Path,default=Path('tests/fixtures/bin/io-uring'))
p.add_argument('--output',type=Path,required=True)
p.add_argument('--cpus',type=int,nargs='+',default=[1,2,4])
p.add_argument('--pressure',action='store_true',help='Also check multishot native TCP full-CQ waits do not busy-spin')
p.add_argument('--abi-only',action='store_true',help='Run only the mapped-ring guest ABI cases; reuse unaffected host TCP checks')
p.add_argument('--tcp-only',action='store_true',help='Reuse already-passed ABI cases when only host-event scheduling changed')
p.add_argument('--require-worker-uring',action='store_true',help='Assert multi-CPU cases actually complete io_uring enters on workers')
p.add_argument('--min-worker-uring',type=int,help='Override the worker ABI fixture minimum for another targeted fixture')
p.add_argument('--require-worker-cq-poll',action='store_true',help='Require worker ppoll/epoll completion in non-pressure TCP cases')
a=p.parse_args(); a.output.mkdir(parents=True,exist_ok=False)
if a.abi_only and a.tcp_only:p.error('--abi-only and --tcp-only are mutually exclusive')
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
report=dict(passed=False,runner_sha256=sha(a.runner),fixture_sha256=sha(a.fixture),rows=[])
def run(label,cpus,extra):
    command=[str(a.runner.resolve()),'--memory','--cpus',str(cpus),'--network','loopback','--metrics','--max-instructions','200000000','--exec-file',f'/uring-self={a.fixture.resolve()}','--guest','/uring-self',*extra]
    before=resource.getrusage(resource.RUSAGE_CHILDREN); began=time.monotonic()
    result=subprocess.run(command,capture_output=True,timeout=30)
    elapsed=time.monotonic()-began; after=resource.getrusage(resource.RUSAGE_CHILDREN)
    host_cpu=after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime
    (a.output/(label+'.stdout')).write_bytes(result.stdout); (a.output/(label+'.stderr')).write_bytes(result.stderr)
    assert result.returncode==0 and result.stdout.endswith(b'io-uring PASS\n'),(label,result.returncode,result.stdout,result.stderr[-2000:])
    metrics=json.loads(next(line.split(': ',1)[1] for line in result.stderr.decode().splitlines() if line.startswith('trames metrics: ')))
    assert metrics['runtime_error'] is None
    if a.require_worker_uring:
        minimum=3 if label.startswith('tcp') else a.min_worker_uring if a.min_worker_uring is not None else 280
        assert metrics['worker_uring_syscalls']>=minimum if cpus>1 else metrics['worker_uring_syscalls']==0,(label,'worker io_uring path not exercised',metrics)
        expected=re.search(rb'^expected-worker-uring: (\d+)$',result.stdout,re.M)
        if expected and cpus>1:
            assert metrics['worker_uring_syscalls']==int(expected[1]),(label,'some io_uring calls left workers',int(expected[1]),metrics)
    if a.require_worker_cq_poll and cpus>1 and label.startswith('tcp') and 'pressure' not in label:
        assert metrics['worker_poll_syscalls']>=2,(label,'CQ poll/epoll did not complete on workers',metrics)
    if 'pressure' in label:
        assert elapsed>=.14 and host_cpu<elapsed*.75,(label,'backpressured wait spun on host CPU',host_cpu,elapsed)
    report['rows'].append(dict(case=label,metrics=metrics,host_child_cpu_seconds=host_cpu,host_wall_seconds=elapsed)); print(label,'PASS',flush=True)
try:
    for cpus in a.cpus:
        if not a.tcp_only:run(f'abi-{cpus}',cpus,[])
        for family,pressure in ([] if a.abi_only else [(f,p) for f in ('4','6') for p in ([False,True] if a.pressure else [False])]):
            errors=[]
            with socket.socket(socket.AF_INET if family=='4' else socket.AF_INET6) as listener:
                listener.bind(('127.0.0.1' if family=='4' else '::1',0)); listener.listen(1); listener.settimeout(10)
                def serve():
                    try:
                        with listener.accept()[0] as peer:
                            peer.settimeout(10); data=b''
                            while len(data)<4:
                                chunk=peer.recv(4-len(data)); assert chunk; data+=chunk
                            assert data==b'ping'
                            if pressure:
                                peer.sendall(bytes(ord('a')+i%26 for i in range(128)))
                            else:
                                time.sleep(.03); peer.sendall(b'p'); time.sleep(.02); peer.sendall(b'ong'); peer.shutdown(socket.SHUT_WR)
                            assert peer.recv(1)==b'', 'request replayed or guest FD retained after close'
                    except Exception as error:
                        errors.append(repr(error))
                thread=threading.Thread(target=serve,daemon=True); thread.start()
                run(f'tcp{family}{"-pressure" if pressure else ""}-{cpus}',cpus,[family+('p' if pressure else ''),str(listener.getsockname()[1])]); thread.join(12)
                assert not thread.is_alive() and not errors,errors
    report['passed']=True
finally:
    (a.output/'results.json').write_text(json.dumps(report,indent=2)+'\n')
