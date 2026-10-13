#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exact original musl integer oracle, string canaries and old-host negotiation."""
import hashlib
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
FIXTURE=ROOT/'tests/fixtures/bin/musl-stdlib'
def run(args,**kw):
    r=subprocess.run(list(map(str,args)),capture_output=True,timeout=300,**kw)
    assert r.returncode==0,(args,r.returncode,r.stdout[-2000:],r.stderr[-4000:])
    assert r.stdout.endswith(b'musl-stdlib-ok\n'),r.stdout[-2000:]
    return r
report=[]
with tempfile.TemporaryDirectory(prefix='trames-stdlib-') as folder:
    tmp=Path(folder);original=ROOT/'target/alpine-build/rootfs'
    expected=run([QEMU,'-cpu','max','-E','TRAMES_ORACLE_SKIP_EXEC=1','-L',original,FIXTURE]).stdout
    hybrid=tmp/'oracle';shutil.copytree(original,hybrid,symlinks=True);shutil.copyfile(LIBC,hybrid/'lib/ld-musl-aarch64.so.1')
    assert run([QEMU,'-cpu','max','-E','TRAMES_ORACLE_SKIP_EXEC=1','-L',hybrid,FIXTURE]).stdout==expected
    for memory in (True,False):
        for enabled in (False,True):
            command=[RUNNER,*(['--memory'] if memory else ['--storage',tmp/f'{memory}-{enabled}']),'--rootfs',ARCHIVE,'--musl-libc',LIBC,'--accelerator','pure' if enabled else 'off','--metrics','--max-instructions','2000000000','--exec-file','/bin/stdlib='+str(FIXTURE),'--guest','/bin/stdlib']
            result=run(command);assert result.stdout==expected,(result.stdout[-2000:],expected[-2000:])
            native,runtime=[json.loads(s.split(': ',1)[1]) for s in result.stderr.decode().splitlines()]
            assert native['faults']==0 and native['rejected']==0,native
            if enabled:
                assert native['integer_conversions']>2500 and all(native['function_calls'][10:17]),native
                assert native['fallbacks']>0 and native['callback_sorts']>0,native
                assert native['validation_hits']>0 and native['validation_misses']>0,native
            else:assert native['calls']==0,native
            report.append(dict(memory=memory,enabled=enabled,native=native,runtime=runtime))
            print(f'PASS stdlib memory={memory} enabled={enabled}: {len(expected.splitlines())-1} exact integer rows, aliases, string search/copy/padding/truncation/guard/COW/shared stack/threads/signals/fork/nested qsort',flush=True)
    baseline=os.getenv('TRAMES_STDLIB_BASELINE_BINARY')
    if baseline:
        result=run([QEMU,'-cpu','max',baseline,'--memory','--rootfs',ARCHIVE,'--musl-libc',LIBC,'--accelerator','pure','--metrics','--max-instructions','2000000000','--exec-file','/bin/stdlib='+str(FIXTURE),'--guest','/bin/stdlib'])
        assert result.stdout==expected
        native=json.loads(result.stderr.decode().splitlines()[0].split(': ',1)[1]);assert len(native['function_calls'])==10 and native['rejected']==0 and native['faults']==0,native
        print('PASS previous numeric host + new full libc: integer/string additions select original musl, no unknown entries',flush=True)
        report.append(dict(previous_host=True,native=native))
(ROOT/'target/musl-hybrid/stdlib-validation.json').write_text(json.dumps(dict(cases=len(expected.splitlines())-1,fixture_sha256=hashlib.sha256(FIXTURE.read_bytes()).hexdigest(),libc_sha256=hashlib.sha256(LIBC.read_bytes()).hexdigest(),runs=report),indent=2)+'\n')
