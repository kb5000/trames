#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Full musl conversions vs original QEMU; value/end/errno and guest callbacks."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
RUNNER=os.getenv('TRAMES_RUNNER',str(ROOT/'target/release/trames-run'))
QEMU=os.getenv('QEMU_AARCH64','qemu-aarch64')
LIBC=ROOT/'target/musl-hybrid/libc.so'
ARCHIVE=ROOT/'target/alpine-download/alpine-minirootfs-3.22.6-aarch64.tar.gz'
FIXTURE=ROOT/'tests/fixtures/bin/musl-numeric'
def run(args,**kw):
    r=subprocess.run(list(map(str,args)),capture_output=True,timeout=240,**kw)
    assert r.returncode==0,(args,r.returncode,r.stdout[-2000:],r.stderr[-4000:])
    return r
def rows(output):
    lines=output.decode().splitlines();assert lines.pop()=='musl-numeric-ok'
    return [[int(x,16 if i in (2,5) else 10) for i,x in enumerate(line.split())] for line in lines]
def close(a,b,bits):
    sign=1<<(bits-1);mask=sign-1;exponent=((1<<(11 if bits==64 else 8))-1)<<(52 if bits==64 else 23)
    # NaN payloads need not match; signed zero/infinity and finite signs must.
    if a&exponent==exponent and a&mask!=exponent:
        return b&exponent==exponent and b&mask!=exponent
    if a==b:return True
    if (a&mask)==0 or (b&mask)==0 or (a&exponent)==exponent or (b&exponent)==exponent:return False
    return a&sign==b&sign and abs(a-b)<=1
with tempfile.TemporaryDirectory(prefix='trames-numeric-') as folder:
    tmp=Path(folder);oracle=tmp/'oracle';shutil.copytree(ROOT/'target/alpine-build/rootfs',oracle,symlinks=True)
    shutil.copyfile(LIBC,oracle/'lib/ld-musl-aarch64.so.1')
    expected=rows(run([QEMU,'-cpu','max','-E','TRAMES_ORACLE_SKIP_EXEC=1','-L',oracle,FIXTURE]).stdout)
    for memory in (True,False):
        for enabled in (False,True):
            cmd=[RUNNER,*(['--memory'] if memory else ['--storage',tmp/f'{memory}-{enabled}']),'--rootfs',ARCHIVE,'--musl-libc',LIBC,'--accelerator','pure' if enabled else 'off','--metrics','--max-instructions','2000000000','--exec-file','/bin/numeric='+str(FIXTURE),'--guest','/bin/numeric']
            result=run(cmd);actual=rows(result.stdout);assert len(actual)==len(expected)
            changed=0
            for index,(a,b) in enumerate(zip(actual,expected)):
                assert a[:2]+a[3:5]+a[6:]==b[:2]+b[3:5]+b[6:],(index,a,b)
                if enabled and a[1]==0:
                    assert close(a[2],b[2],64) and close(a[5],b[5],32),(index,a,b)
                    changed+=a[2]!=b[2] or a[5]!=b[5]
                else:assert a==b,(index,a,b)
            native,runtime=[json.loads(s.split(': ',1)[1]) for s in result.stderr.decode().splitlines()]
            assert native['faults']==0 and native['rejected']==0,native
            if enabled:assert native['numeric_conversions']>3000 and native['numeric_replays']>0 and native['fallbacks']>0 and native['callback_sorts']>0,native
            else:assert native['calls']==0,native
            print(f'PASS numeric memory={memory} enabled={enabled}: {len(actual)} cases, {changed} differences <=1 ULP; errno/end exact, rounding fallback, threads/shared stack/signals/fork/nested qsort',flush=True)
