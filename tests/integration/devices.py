#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Guest device inodes with native Linux driver I/O comparison."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
RUNNER=os.getenv('TRAMES_RUNNER',str(ROOT/'target/release/trames-run'))
with tempfile.TemporaryDirectory(prefix='trames-devices-') as directory:
    tmp=Path(directory)
    native=tmp/'native'
    subprocess.run([os.getenv('CC','cc'),'-O2','-Wall','-Wextra','-Werror',str(ROOT/'tests/fixtures/devices.c'),'-o',str(native)],check=True)
    oracle=subprocess.run([str(native),'native'],check=True,capture_output=True).stdout
    for cpus in ('1','4'):
        base=[RUNNER,'--storage',str(tmp/cpus),'--cpus',cpus,'--network','off','--max-instructions','500000000']
        p=subprocess.run([*base,'--exec-file',f"/devices={ROOT/'tests/fixtures/bin/devices'}",'--guest','/devices'],capture_output=True,timeout=60)
        assert p.returncode==0 and p.stdout==oracle+b'device nodes: numbers, permissions, hard links, directory types, PTY reuse, nodev and readonly: ok\n',(cpus,p.returncode,p.stdout,p.stderr)
        p=subprocess.run([*base,'--guest','/devices','reopen'],capture_output=True,timeout=60)
        assert p.returncode==0 and p.stdout==b'device inode metadata and behavior survive disk reopen: ok\n',(cpus,p.returncode,p.stdout,p.stderr)
        print(f'{cpus}-CPU guest mknodat, device I/O vs native Linux, permission/mount checks and persistent reopen: passed',flush=True)
    native=tmp/'native-openat2'
    subprocess.run([os.getenv('CC','cc'),'-O2','-Wall','-Wextra','-Werror',str(ROOT/'tests/fixtures/openat2.c'),'-o',str(native)],check=True)
    ref=tmp/'native-directory';ref.mkdir()
    oracle=subprocess.run([str(native)],cwd=ref,check=True,capture_output=True).stdout
    for cpus in ('1','4'):
        base=[RUNNER,'--storage',str(tmp/('openat2-'+cpus)),'--cpus',cpus,'--network','off']
        p=subprocess.run([*base,'--exec-file',f"/openat2={ROOT/'tests/fixtures/bin/openat2'}",'--guest','/openat2','mounts'],capture_output=True,timeout=60)
        assert p.returncode==0 and p.stdout.startswith(oracle) and p.stdout.count(b': ok\n')==oracle.count(b': ok\n')+4,(cpus,p.returncode,p.stdout,p.stderr)
        print(f'{cpus}-CPU openat2: {oracle.count(b": ok")} native Linux comparisons plus tmpfs/bind traversal and cached-lookup checks passed',flush=True)
