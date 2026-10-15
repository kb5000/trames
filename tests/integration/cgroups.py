#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Guest cgroup v2 domains; this does not claim resource controllers."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
RUNNER=os.getenv('TRAMES_RUNNER',str(ROOT/'target/release/trames-run'))
with tempfile.TemporaryDirectory(prefix='trames-cgroup-') as directory:
    native=Path(directory)/'dumpable-native'
    subprocess.run([os.getenv('CC','cc'),'-O2','-Wall','-Wextra','-Werror','-pthread',str(ROOT/'tests/fixtures/dumpable.c'),'-o',str(native)],check=True)
    oracle=subprocess.run([str(native)],capture_output=True,check=True).stdout
    for cpus in ('1','4'):
        base=[RUNNER,'--storage',str(Path(directory)/cpus),'--network','off','--cpus',cpus,'--max-instructions','1000000000']
        p=subprocess.run([*base,'--exec-file',f"/cgroups={ROOT/'tests/fixtures/bin/cgroups'}",'--guest','/cgroups'],capture_output=True,timeout=90)
        assert p.returncode==0 and p.stdout==b'cgroup v2: hierarchy, membership, threads/exec/exit, PID views, mounts, stale FDs and denied controls: ok\n',(cpus,p.returncode,p.stdout,p.stderr[-8000:])
        print(f'{cpus}-CPU cgroup hierarchy, lifecycle, PID/mount views and control validation: passed',flush=True)
        p=subprocess.run([*base,'--guest','/cgroups','fresh'],capture_output=True,timeout=60)
        assert p.returncode==0 and b'not persisted' in p.stdout,(p.returncode,p.stdout,p.stderr)
        print(f'{cpus}-CPU cgroup session state reset with root disk preserved: passed',flush=True)

        p=subprocess.run([*base,'--exec-file',f"/dumpable={ROOT/'tests/fixtures/bin/dumpable'}",'--guest','/dumpable'],capture_output=True,timeout=90)
        assert p.returncode==0 and p.stdout==oracle,(cpus,p.returncode,p.stdout,p.stderr[-8000:])
        p=subprocess.run([*base,'--guest','/dumpable','ids'],capture_output=True,timeout=60)
        assert p.returncode==0 and p.stdout==b'dumpable credential transitions: ok\n',(p.returncode,p.stdout,p.stderr)
        print(f'{cpus}-CPU dumpable behavior matches native Linux; UID transitions passed',flush=True)
