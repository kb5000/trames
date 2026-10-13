#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""One alternating baseline/1/2/4 CPU batch; fixed work, exact output checks."""
import argparse,hashlib,json,os,platform,re,resource,statistics,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline',type=Path,required=True)
p.add_argument('--baseline-commit',required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--loops',type=int,default=10000000)
a=p.parse_args()
current=ROOT/'target/release/trames-run';fixture=ROOT/'tests/fixtures/bin/smp';memory=ROOT/'tests/fixtures/bin/cpu-memory'
h=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
expected=subprocess.run([os.getenv('QEMU_AARCH64','qemu-aarch64'),'-cpu','max',str(fixture),'work','4',str(a.loops)],capture_output=True,timeout=60)
assert expected.returncode==0 and not expected.stderr,expected
rows=[]
for workload in ('four-thread-compute','single-thread-memory'):
    modes=[('baseline',a.baseline,None),('1',current,1)]
    if workload=='four-thread-compute':modes+=[('2',current,2),('4',current,4)]
    samples={name:[] for name,_,_ in modes}
    for sample in range(3):
        order=modes if sample%2==0 else list(reversed(modes))
        for name,binary,count in order:
            cmd=[str(binary),'--memory','--network','off','--metrics','--max-instructions','10000000000']
            if count:cmd+=['--cpus',str(count)]
            cmd+= [str(fixture),'work','4',str(a.loops)] if workload=='four-thread-compute' else [str(memory)]
            before=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.monotonic()
            runs=3 if workload=='single-thread-memory' else 1
            results=[subprocess.run(cmd,capture_output=True,timeout=300) for _ in range(runs)]
            r=results[-1];wall=time.monotonic()-start
            after=resource.getrusage(resource.RUSAGE_CHILDREN)
            want=expected.stdout if workload=='four-thread-compute' else b'cpu-memory-ok\n'
            assert all((r.returncode,r.stdout)==(0,want) for r in results),(name,r.returncode,r.stdout,r.stderr[-4000:])
            lines=r.stderr.decode().splitlines();assert len(lines)==2,lines
            native=json.loads(lines[0].split(': ',1)[1]);runtime=json.loads(lines[1].split(': ',1)[1])
            runtime['seconds']=sum(json.loads(x.stderr.decode().splitlines()[1].split(': ',1)[1])['seconds'] for x in results)/runs
            assert native['faults']==native['rejected']==0,native
            value=dict(runtime=runtime,wall_seconds=wall,host_cpu_seconds=after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime)
            samples[name].append(value)
            print(workload,sample,name,runtime['seconds'],flush=True)
    medians={name:statistics.median(x['runtime']['seconds'] for x in values) for name,values in samples.items()}
    rows.append(dict(workload=workload,samples=samples,median_seconds=medians,
        speedup_vs_current_1={name:medians['1']/seconds for name,seconds in medians.items()},
        baseline_over_current_1=medians['baseline']/medians['1']))
report=dict(baseline_commit=a.baseline_commit,baseline_sha256=h(a.baseline),candidate_sha256=h(current),fixture_sha256=h(fixture),memory_fixture_sha256=h(memory),
    host=platform.machine(),cpu_quota=Path('/sys/fs/cgroup/cpu.max').read_text().strip(),
    cpu_affinity=len(os.sched_getaffinity(0)),guest='aarch64',compiler='Rust 1.90.0 release/all-features',
    loops_per_guest_thread=a.loops,guest_threads=4,memory_runs_per_sample=3,rows=rows,
    source_sha256={str(path.relative_to(ROOT)):h(path) for path in sorted((ROOT/'crates').glob('*/src/*.rs'))},
    limits='Development x86_64 host with four-core cgroup quota; no physical iPad timing or general application speedup claim.')
a.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps([{'workload':r['workload'],'medians':r['median_seconds'],'speedup':r['speedup_vs_current_1'],'baseline_over_current_1':r['baseline_over_current_1']} for r in rows],indent=2))
