#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""NativeCall v1: actual ARM64 native routines and ordinary guest ELF stubs."""
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
UPSTREAM=ROOT/'crates/trames-native/upstream/musl-1.2.5'
provenance=json.loads((UPSTREAM/'provenance.json').read_text())
for name,digest in provenance['files'].items():
    source=UPSTREAM/name.removeprefix('src/')
    if not source.is_file(): source=UPSTREAM/name.removeprefix('src/string/')
    if not source.is_file() and name.startswith('src/malloc/mallocng/'):
        source=UPSTREAM/'mallocng'/name.removeprefix('src/malloc/mallocng/')
    assert hashlib.sha256(source.read_bytes()).hexdigest()==digest,name

def run(args):
    r=subprocess.run(args,capture_output=True,timeout=180)
    assert r.returncode==0,(args,r.returncode,r.stdout[-4000:],r.stderr[-4000:])
    return r

def metrics(result):
    text=result.stderr.decode()
    native=json.loads(next(s.split(': ',1)[1] for s in text.splitlines() if s.startswith('trames native metrics: ')))
    runtime=json.loads(next(s.split(': ',1)[1] for s in text.splitlines() if s.startswith('trames metrics: ')))
    assert 'trames: ' not in text,text
    return native,runtime

with tempfile.TemporaryDirectory(prefix='trames-accelerator-') as directory:
    tmp=Path(directory); oracle=tmp/'oracle'; (oracle/'lib').mkdir(parents=True); (oracle/'bin').mkdir()
    loader=ROOT/'target/alpine-build/rootfs/lib/ld-musl-aarch64.so.1'
    shutil.copyfile(loader,oracle/'lib/ld-musl-aarch64.so.1')
    (oracle/'lib/libc.so').symlink_to('ld-musl-aarch64.so.1')
    library=ROOT/'target/accelerator-guest/libtrames-pure.so'
    shutil.copyfile(library,oracle/'lib/libtrames-pure.so')
    for kind in ['accelerator','accelerator-dynamic']:
        fixture=ROOT/'tests/fixtures/bin'/kind
        shutil.copyfile(fixture,oracle/'bin/accelerator-test'); (oracle/'bin/accelerator-test').chmod(0o755)
        # Local QEMU user has no ARM binfmt host exec; Trames still tests real exec.
        expected=run([QEMU,'-cpu','max','-E','TRAMES_ORACLE_SKIP_EXEC=1','-L',str(oracle),str(fixture)])
        assert expected.stdout==b'accelerator-pure-ok\n',expected
        for store in [['--memory'],['--storage',str(tmp/kind)]]:
            imports=['--exec-file','/bin/accelerator-test='+str(fixture),'--exec-file','/lib/ld-musl-aarch64.so.1='+str(loader),'--exec-file','/lib/libtrames-pure.so='+str(library)]
            for enabled in [False,True]:
                # Each run imports into a fresh disk store to avoid overwrite semantics.
                storage=store if store[0]=='--memory' else ['--storage',str(tmp/f'{kind}-{enabled}')]
                args=[RUNNER,*storage,'--metrics','--max-instructions','2000000000',*imports]
                if enabled: args+=['--accelerator','pure']
                actual=run([*args,'--guest','/bin/accelerator-test'])
                assert actual.stdout==expected.stdout,actual
                native,_=metrics(actual)
                if enabled:
                    assert native['calls']>100 and native['fallbacks']>0 and native['rejected']==1 and native['faults']==3,native
                    assert native['bytes_written']>1000000,native
                else: assert native['calls']==0 and native['rejected']==0,native
                print('PASS',kind,store[0],'enabled='+str(enabled),'ordinary ELF, dlsym, original fallback, COW/shared/EOF, threads/exec, precise faults and code-cache invalidation',flush=True)

    # Actual unmodified Alpine BusyBox uses the installed ELF through LD_PRELOAD.
    # This pipe/exec path runs in Trames; local QEMU has no ARM binfmt exec hook.
    for enabled in [False,True]:
        args=[RUNNER,'--memory','--metrics','--max-instructions','100000000',*imports,'--exec-file','/bin/busybox='+str(ROOT/'target/alpine-build/rootfs/bin/busybox'),'--env','LD_PRELOAD=/lib/libtrames-pure.so']
        if enabled: args+=['--accelerator','pure']
        actual=run([*args,'--guest','/bin/busybox','sh','-c','printf accelerator-preload-ok | /bin/busybox wc -c'])
        assert actual.stdout.strip()==b'22',actual
        native,_=metrics(actual)
        assert native['faults']==0 and (native['calls']>0 if enabled else native['calls']==0),native
        print('PASS real Alpine BusyBox LD_PRELOAD pipe/exec enabled='+str(enabled),flush=True)
