#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Complete standard-path musl; no preload or separate acceleration library."""
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
ARCHIVE=Path(os.getenv('TRAMES_ALPINE_CACHE',str(ROOT/'target/alpine-download')))/'alpine-minirootfs-3.22.6-aarch64.tar.gz'
LIBC=ROOT/'target/musl-hybrid/libc.so'
manifest=json.loads((LIBC.parent/'manifest.json').read_text())
assert hashlib.sha256(LIBC.read_bytes()).hexdigest()==manifest['libc_sha256']
def run(args,expected=0,**kw):
    r=subprocess.run(list(map(str,args)),capture_output=True,timeout=240,**kw)
    assert r.returncode==expected,(args,r.returncode,r.stdout[-2000:],r.stderr[-4000:])
    return r
def metrics(r):
    lines=r.stderr.decode().splitlines();assert len(lines)==2,r.stderr
    native=json.loads(lines[0].split(': ',1)[1]);runtime=json.loads(lines[1].split(': ',1)[1])
    return native,runtime
with tempfile.TemporaryDirectory(prefix='trames-full-musl-') as folder:
    tmp=Path(folder);oracle=tmp/'oracle';shutil.copytree(ROOT/'target/alpine-build/rootfs',oracle,symlinks=True)
    shutil.copyfile(LIBC,oracle/'lib/ld-musl-aarch64.so.1')
    (oracle/'test').mkdir();shutil.copyfile(ROOT/'tests/fixtures/bin/libm3probe.so',oracle/'test/libm3probe.so')
    # Compare the standard dynamic export set against Alpine's existing musl.
    readelf=os.getenv('AARCH64_READELF','aarch64-linux-gnu-readelf')
    def exports(path):
        text=run([readelf,'--dyn-syms','-W',path]).stdout.decode();out=set()
        for line in text.splitlines():
            f=line.split()
            if len(f)>=8 and f[4] in ('GLOBAL','WEAK') and f[5]=='DEFAULT' and f[6]!='UND':out.add(f[7])
        return out
    original=exports(ROOT/'target/alpine-build/rootfs/lib/ld-musl-aarch64.so.1');hybrid=exports(LIBC)
    assert original<=hybrid,sorted(original-hybrid)
    assert not any('trames' in name for name in hybrid)
    print('PASS full musl public exports:',len(hybrid),'all Alpine exports retained; private helpers hidden',flush=True)
    for name,args,wanted in [('accelerator-full-musl',[],b'accelerator-pure-ok\n'),('musl-startup',[],b'musl-startup-ok\n'),('runtime-compat-dynamic',[],b'runtime-compat-ok\n'),('musl-callbacks',[],None)]:
        fixture=ROOT/'tests/fixtures/bin'/name
        expected=run([QEMU,'-cpu','max','-E','TRAMES_ORACLE_SKIP_EXEC=1','-L',oracle,fixture,*args],cwd=oracle/'tmp')
        if wanted is not None: assert expected.stdout==wanted,expected
        else: assert expected.stdout.startswith(b'musl-callbacks-ok '),expected
        for memory in (True,False):
            for enabled in (False,True):
                store=tmp/f'{name}-{memory}-{enabled}'
                cmd=[RUNNER,*(['--memory'] if memory else ['--storage',store]),'--rootfs',ARCHIVE,'--musl-libc',LIBC,'--accelerator','pure' if enabled else 'off','--metrics','--max-instructions','2000000000','--exec-file','/bin/accelerator-test='+str(fixture),'--exec-file','/bin/runtime-compat='+str(fixture),'--exec-file','/test/libm3probe.so='+str(ROOT/'tests/fixtures/bin/libm3probe.so'),'--guest','/bin/accelerator-test',*args]
                if name=='musl-callbacks' and enabled: cmd.insert(1,'--native-bsearch')
                actual=run(cmd);assert actual.stdout==expected.stdout,actual
                native,_=metrics(actual)
                assert native['calls']>0 if enabled else native['calls']==0,native
                if name=='accelerator-full-musl' and enabled:
                    assert native['fallbacks']>0 and native['rejected']==1 and native['faults']>=1,native
                    assert all(native['function_calls'][5:10]),native
                    assert native['direct_bytes_read']>1000000 and native['direct_bytes_written']>1000000,native
                    assert native['buffered_bytes_read']>0 and native['buffered_bytes_written']>0,native
                if name!='accelerator-full-musl':assert native['faults']==0 and native['rejected']==0,native
                if name=='musl-callbacks' and enabled:
                    assert native['callback_requests']>1000 and native['callback_sorts']>30 and native['callback_searches']>100,native
                    assert native['fallbacks']>0 and native['function_calls'][8]>0,native
                print('PASS',name,'memory='+str(memory),'enabled='+str(enabled),flush=True)
    # Upgrade an existing environment, preserve user data, reopen idempotently,
    # keep ordinary libc alias and execute the loader directly at its normal path.
    store=tmp/'persistent'
    run([RUNNER,'--storage',store,'--rootfs',ARCHIVE,'--guest','/bin/busybox','sh','-c','printf preserved > /root/user-data'])
    for i in range(2):
        out=run([RUNNER,'--storage',store,'--musl-libc',LIBC,'--accelerator','pure','--metrics','--guest','/bin/busybox','sh','-c','cat /root/user-data; readlink /lib/libc.musl-aarch64.so.1; printf ordinary-pipe | /bin/busybox wc -c'])
        assert out.stdout==b'preservedld-musl-aarch64.so.1\n13\n',out
        assert metrics(out)[0]['calls']>0
    direct=run([RUNNER,'--storage',store,'--accelerator','pure','--metrics','--guest','/lib/ld-musl-aarch64.so.1','/bin/busybox','echo','loader-direct-ok'])
    assert direct.stdout==b'loader-direct-ok\n' and metrics(direct)[0]['calls']>0,direct
    invalid=tmp/'invalid';invalid.write_bytes(b'not ELF')
    run([RUNNER,'--storage',store,'--musl-libc',invalid,'--guest','/bin/busybox','true'],expected=125)
    exported=tmp/'after-failure.so'
    run([RUNNER,'--storage',store,'--export','/lib/ld-musl-aarch64.so.1='+str(exported),'--guest','/bin/busybox','true'])
    assert exported.read_bytes()==LIBC.read_bytes()
    run([RUNNER,'--storage',store,'--guest','/bin/busybox','sh','-c','printf user-file > /lib/.ld-musl-aarch64.so.1.new'])
    run([RUNNER,'--storage',store,'--musl-libc',ROOT/'target/alpine-build/rootfs/lib/ld-musl-aarch64.so.1','--guest','/bin/busybox','true'],expected=125)
    out=run([RUNNER,'--storage',store,'--guest','/bin/busybox','cat','/lib/.ld-musl-aarch64.so.1.new']);assert out.stdout==b'user-file'
    print('PASS standard-path upgrade, user data, idempotent reopen, pipe/exec, direct loader and invalid ELF rollback',flush=True)
