#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Original allocator oracle, mixed hot/cold paths, cross-thread free and fork."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
RUNNER=os.getenv('TRAMES_RUNNER',str(ROOT/'target/release/trames-run'))
LIBC=ROOT/'target/musl-hybrid/libc.so'
FIXTURE=ROOT/'tests/fixtures/bin/musl-allocator'
ARCHIVE=ROOT/'target/alpine-download/alpine-minirootfs-3.22.6-aarch64.tar.gz'
def run(args):
    result=subprocess.run(list(map(str,args)),capture_output=True,timeout=300)
    assert result.returncode==0,(args,result.returncode,result.stdout[-1000:],result.stderr[-4000:])
    assert result.stdout==b'musl-allocator-ok\n',result.stdout[-1000:]
    return result
rows=[]
with tempfile.TemporaryDirectory(prefix='trames-allocator-') as directory:
    tmp=Path(directory)
    original=ROOT/'target/alpine-build/rootfs'
    run([os.getenv('QEMU_AARCH64','qemu-aarch64'),'-cpu','max','-E','TRAMES_ORACLE_SKIP_EXEC=1','-L',original,FIXTURE])
    hybrid=tmp/'oracle';shutil.copytree(original,hybrid,symlinks=True);shutil.copyfile(LIBC,hybrid/'lib/ld-musl-aarch64.so.1')
    run([os.getenv('QEMU_AARCH64','qemu-aarch64'),'-cpu','max','-E','TRAMES_ORACLE_SKIP_EXEC=1','-L',hybrid,FIXTURE])
    for enabled,cpus,shared,memory in [(False,1,False,True),(False,4,False,True),('default',4,False,True),(True,1,False,True),(True,2,False,True),(True,4,False,True),(True,4,True,True),(True,4,False,False)]:
        args=[RUNNER,*(['--memory'] if memory else ['--storage',tmp/'store']),'--rootfs',ARCHIVE,'--musl-libc',LIBC,'--cpus',str(cpus),'--accelerator','pure' if enabled else 'off','--metrics','--max-instructions','2000000000','--exec-file','/bin/allocator='+str(FIXTURE),'--guest','/bin/allocator',*(['shared-stack'] if shared else [])]
        if enabled is True:args.insert(1,'--native-allocator')
        result=run(args)
        native,runtime=[json.loads(line.split(': ',1)[1]) for line in result.stderr.decode().splitlines()]
        assert not native['faults'] and not native['rejected'],native
        if enabled is True:
            assert native['allocator_allocations']>3000 and native['allocator_frees']>1000,native
            assert native['allocator_calls']>native['allocator_allocations']+native['allocator_frees'],native
            assert native['allocator_journal_writes']>native['allocator_frees'],native
            if cpus==1:
                assert native['allocator_load_callbacks']==0,native
                assert native['allocator_direct_loads']>2*native['allocator_page_callbacks'],native
            else:assert native['allocator_load_callbacks']>0,native
            if cpus>1:assert native['worker_calls']>3000,native
        elif enabled=='default':assert native['calls']>0 and native['allocator_calls']==0,native
        else:assert native['calls']==0,native
        rows.append(dict(enabled=enabled,cpus=cpus,shared_stack=shared,memory=memory,native=native,runtime=runtime))
        print(f'PASS mallocng cpus={cpus} enabled={enabled} shared_stack={shared} memory={memory}: malloc={native["allocator_allocations"]} free={native["allocator_frees"]} resumed={native["allocator_free_resumes"]}',flush=True)
(ROOT/'target/musl-hybrid/allocator-validation.json').write_text(json.dumps(dict(fixture_sha256=hashlib.sha256(FIXTURE.read_bytes()).hexdigest(),libc_sha256=hashlib.sha256(LIBC.read_bytes()).hexdigest(),rows=rows),indent=2)+'\n')
