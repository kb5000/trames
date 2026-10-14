#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent ordinary CPU accesses in different regions of one guest page."""
import json, os, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
fixture=ROOT/'tests/fixtures/bin/region-memory'
libc=ROOT/'target/musl-hybrid/libc.so'
archive=ROOT/'target/alpine-download/alpine-minirootfs-3.22.6-aarch64.tar.gz'
runner=os.getenv('TRAMES_RUNNER',str(ROOT/'target/release/trames-run'))
oracle=subprocess.run([os.getenv('QEMU_AARCH64','qemu-aarch64'),'-cpu','max','-L',str(ROOT/'target/alpine-build/rootfs'),str(fixture),'5000'],capture_output=True,timeout=60)
assert oracle.returncode==0 and not oracle.stderr and oracle.stdout==b'region-memory-ok 20000\n',oracle
for count in (1,2,4):
    result=subprocess.run([runner,'--memory','--network','off','--rootfs',str(archive),'--musl-libc',str(libc),'--cpus',str(count),'--accelerator','pure','--metrics','--max-instructions','100000000','--exec-file','/bin/regions='+str(fixture),'--guest','/bin/regions','5000'],capture_output=True,timeout=180)
    assert (result.returncode,result.stdout)==(0,oracle.stdout),(result.returncode,result.stdout,result.stderr[-4000:])
    native,runtime=[json.loads(line.split(': ',1)[1]) for line in result.stderr.decode().splitlines()]
    assert native['faults']==native['rejected']==0,native
    if count>1:assert runtime['parallel_rounds']>0 and runtime['peak_parallel_cpus']>=2,runtime
    print(f'PASS same-page independent CPU regions cpus={count}: exact four-counter result',flush=True)
