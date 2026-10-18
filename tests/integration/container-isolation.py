#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real guest namespace/FD/network policy and seccomp vs independent native Linux."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[2]
BIN=ROOT/'tests/fixtures/bin'
OUT=ROOT/'target/container-isolation'
OUT.mkdir(parents=True,exist_ok=True)
runner=os.environ.get('TRAMES_RUNNER',str(ROOT/'target/release/trames-run'))
oracle=subprocess.run([str(BIN/'seccomp-native')],capture_output=True,timeout=30)
assert oracle.returncode==0 and not oracle.stderr,(oracle.returncode,oracle.stdout,oracle.stderr)
(OUT/'seccomp-native.log').write_bytes(oracle.stdout)
checks=[]
for cpus in ('1','4'):
    for name in ('seccomp','network-namespaces'):
        p=subprocess.run([runner,'--memory','--network','full','--cpus',cpus,
            '--max-instructions','300000000','--exec-file',f'/{name}={BIN/name}',
            '--guest','/'+name],capture_output=True,timeout=60)
        (OUT/f'{name}-{cpus}.log').write_bytes(p.stdout+p.stderr)
        assert p.returncode==0 and not p.stderr,(cpus,name,p.returncode,p.stdout,p.stderr)
        if name=='seccomp': assert p.stdout==oracle.stdout,(p.stdout,oracle.stdout)
        else: assert p.stdout==b'network namespaces: private TCP/UDP/Unix, old FDs, pathname reachability, lo, sysctls and setns: ok\n',p.stdout
        checks.append(dict(cpus=int(cpus),fixture=name,exit_code=p.returncode,stdout=p.stdout.decode()))
        print(f'{cpus}-CPU {name}: passed'+(' (matches native Linux)' if name=='seccomp' else ''),flush=True)
report=dict(checks=checks,native_seccomp=oracle.stdout.decode(),
    source_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/'tests/fixtures/seccomp.c',ROOT/'tests/fixtures/network-namespaces.c']},
    fixture_sha256={name:hashlib.sha256((BIN/name).read_bytes()).hexdigest() for name in ('seccomp','network-namespaces','seccomp-native')},
    scope='Guest net/IPC/cgroup namespaces and enforced classic-BPF seccomp. Namespace tests do not claim a privileged host netns oracle, bridge/NAT or a physical iPad.')
(OUT/'results.json').write_text(json.dumps(report,indent=2)+'\n')
