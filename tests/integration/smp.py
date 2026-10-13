#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real AArch64 pthread/atomic/futex programs, 1/2/4 virtual CPU comparison."""
import json,os,re,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
fixture=ROOT/'tests/fixtures/bin/smp'
qemu=os.getenv('QEMU_AARCH64','qemu-aarch64')
oracle=subprocess.run([qemu,'-cpu','max',str(fixture)],env={**os.environ,'TRAMES_SMP_ORACLE':shutil.which(qemu)},capture_output=True,timeout=120)
assert oracle.returncode==0 and not oracle.stderr,(oracle.returncode,oracle.stdout,oracle.stderr)
runner=os.getenv('TRAMES_RUNNER',str(ROOT/'target/release/trames-run'))
work=subprocess.run([qemu,'-cpu','max',str(fixture),'work','4','300000'],capture_output=True,timeout=60)
assert work.returncode==0 and not work.stderr,work
for count in (1,2,4):
    result=subprocess.run([runner,'--memory','--cpus',str(count),'--metrics','--env',f'TRAMES_SMP_CPU_COUNT={count}',
        '--exec-file','/smp='+str(fixture),'--env','TRAMES_SMP_EXEC_PATH=/smp',
        '--max-instructions','1000000000',str(fixture)],capture_output=True,timeout=240)
    assert result.returncode==0,(count,result.returncode,result.stdout,result.stderr[-8000:])
    assert result.stdout==b'smp: topology, affinity and getcpu: passed\n'+oracle.stdout,(count,result.stdout,oracle.stdout)
    m=re.search(rb'trames metrics: (\{[^\n]+\})',result.stderr);assert m,result.stderr
    metrics=json.loads(m[1]);assert metrics['cpu_count']==count,metrics
    if count>1:assert metrics['parallel_rounds']>0 and metrics['worker_slices']>=2*metrics['parallel_rounds'] and metrics['peak_parallel_cpus']>1,metrics
    else:assert metrics['parallel_rounds']==0,metrics
    print(f'{count} CPU:',json.dumps(metrics),flush=True)
    sustained=subprocess.run([runner,'--memory','--cpus',str(count),'--metrics',
        '--max-instructions','100000000',str(fixture),'work','4','300000'],capture_output=True,timeout=120)
    assert (sustained.returncode,sustained.stdout)==(0,work.stdout),sustained
    m=re.search(rb'trames metrics: (\{[^\n]+\})',sustained.stderr);assert m,sustained.stderr
    profile=json.loads(m[1])
    if count>1:assert profile['parallel_rounds']>0 and profile['worker_chunks']>0,profile
    else:assert profile['worker_chunks']==profile['parallel_rounds']==0,profile
    print(f'{count} CPU sustained fixed work:',json.dumps(profile),flush=True)
print(oracle.stdout.decode(),end='')
print('SMP: native Linux/QEMU and interpreter 1/2/4 CPU workflows agree')
