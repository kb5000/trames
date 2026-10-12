#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real shared/private/anonymous mappings, memfd sealing and file APIs against QEMU."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tests/fixtures/bin/mmap-coverage'
RUNNER=os.getenv('TRAMES_RUNNER',str(ROOT/'target/debug/trames-run'))
QEMU=os.getenv('QEMU_AARCH64','qemu-aarch64')
with tempfile.TemporaryDirectory(prefix='trames-mmap-') as directory:
    oracle=subprocess.run([QEMU,'-cpu','max',str(FIXTURE)],cwd=directory,capture_output=True,timeout=60)
    assert oracle.returncode==0 and oracle.stdout==b'mmap-coverage-ok\n',oracle
    scheduled=subprocess.run([str(ROOT/"tests/fixtures/bin/mmap-coverage-host"),"scheduler"],capture_output=True,timeout=30)
    assert (scheduled.returncode,scheduled.stdout,scheduled.stderr)==(0,b"mmap-coverage-ok\n",b""),scheduled
    for store in (['--memory'],['--storage',directory+'/store']):
        actual=subprocess.run([RUNNER,*store,str(FIXTURE)],capture_output=True,timeout=60)
        assert (actual.returncode,actual.stdout,actual.stderr)==(0,oracle.stdout,b''),actual
        scheduled=subprocess.run([RUNNER,*store,str(FIXTURE),"scheduler"],capture_output=True,timeout=30)
        assert (scheduled.returncode,scheduled.stdout,scheduled.stderr)==(0,b"mmap-coverage-ok\n",b""),scheduled
        if store[0]=='--storage':
            for mode in ('persist-write','persist-read'):
                persistent=subprocess.run([RUNNER,*store,str(FIXTURE),mode],capture_output=True,timeout=30)
                assert (persistent.returncode,persistent.stdout,persistent.stderr)==(0,b'mmap-coverage-ok\n',b''),persistent
        print('PASS',store[0],'shared/private/anonymous mmap, fork, EOF/truncate SIGBUS, COW discard, remap, memfd/seals, code alias invalidation, file APIs and virtual scheduler')
