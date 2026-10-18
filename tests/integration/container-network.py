#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real ARM64 guest netlink/FD/veth/bridge data path; no privileged host netns oracle."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cpus',choices=('1','4','both'),default='both')
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
out=root/'target/container-network';out.mkdir(parents=True,exist_ok=True)
runner=os.environ.get('TRAMES_RUNNER',str(root/'target/release/trames-run'))
expected=b'container network: veth/bridge, namespace FDs, live TCP/UDP, sysfs/proc and cross-bridge isolation: ok\n'
checks=[]
for cpus in (('1','4') if args.cpus=='both' else (args.cpus,)):
    for fixture in ('container-network','network-namespaces'):
        binary=root/'tests/fixtures/bin'/fixture
        wanted=expected if fixture=='container-network' else b'network namespaces: private TCP/UDP/Unix, old FDs, pathname reachability, lo, sysctls and setns: ok\n'
        result=subprocess.run([runner,'--memory','--network','full','--cpus',cpus,
            '--max-instructions','300000000','--exec-file',f'/{fixture}={binary}',
            '--guest','/'+fixture],capture_output=True,timeout=60)
        (out/f'{fixture}-{cpus}.log').write_bytes(result.stdout+result.stderr)
        assert result.returncode==0 and result.stdout==wanted and not result.stderr,(cpus,fixture,result.returncode,result.stdout,result.stderr)
        checks.append(dict(cpus=int(cpus),fixture=fixture,exit_code=result.returncode,stdout=result.stdout.decode()))
        print(f'{cpus}-CPU {fixture}: passed',flush=True)
report=dict(checks=checks,source_sha256=hashlib.sha256((root/'tests/fixtures/container-network.c').read_bytes()).hexdigest(),
    fixture_sha256={name:hashlib.sha256((root/'tests/fixtures/bin'/name).read_bytes()).hexdigest() for name in ('container-network','network-namespaces')},
    scope='Namespace-owned configurable veth/bridge, direct/static routes and real internal IP data. External forwarding, nftables/NAT, host-to-container paths and full Docker bridge are not accepted by this fixture.')
(out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
