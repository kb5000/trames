#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Original signed iptables-nft controls enforce real guest packet filtering.

No host firewall commands/daemon, rootless substitutions or modified guest tools.
Reuses unchanged Docker bridge acceptance; NAT/conntrack deliberately not accepted.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import m6

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cpus', choices=('1', '4', 'both'), default='both')
args = parser.parse_args()
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'target/nftables'
OUT.mkdir(parents=True, exist_ok=True)
RUNNER = os.getenv('TRAMES_RUNNER', str(ROOT / 'target/release/trames-run'))
FIXTURE = ROOT / 'tests/fixtures/bin/nftables'
marker = b'nftables: original iptables-nft/ip6tables, IPv4/IPv6 DROP/ACCEPT, counters, rule lifecycle and namespace policy: ok\n'
results = {'scope': 'Namespace-local bounded nftables filter only: original signed iptables-nft/ip6tables rule creation/list/check/insert/replace/delete/flush/policy, routed IPv4/IPv6 port/source/interface filtering, packet counters, namespace INPUT policy, rejected unsupported conntrack/NAT. No default Docker firewall/NAT/external-network acceptance.',
           'inputs_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in
                             (FIXTURE, ROOT / 'tests/fixtures/nftables.c', ROOT / 'tests/fixtures/container-network.c')},
           'checks': []}
with tempfile.TemporaryDirectory(prefix='trames-nftables-') as directory:
    tmp = Path(directory)
    m6.repository(tmp / 'repository', ('iptables',))
    packages = [p for p in sorted((tmp / 'repository').rglob('*.apk'))
                if not p.name.startswith(('dash-', 'yash-'))]
    results['packages_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in packages}
    rootfs = m6.CACHE / 'alpine-minirootfs-3.22.6-aarch64.tar.gz'
    results['rootfs_sha256'] = hashlib.sha256(rootfs.read_bytes()).hexdigest()
    base = [RUNNER, '--storage', str(tmp / 'store'), '--network', 'full', '--max-instructions', '1000000000']
    imported = [arg for p in packages for arg in ('--file', '/tmp/' + p.name + '=' + str(p))]
    install = subprocess.run([*base, '--cpus', '1', '--rootfs', str(rootfs), *imported,
        '--guest', '/sbin/apk', '--repositories-file', '/dev/null', '--no-progress', 'add',
        *['/tmp/' + p.name for p in packages]], capture_output=True, timeout=120)
    (OUT / 'install.log').write_bytes(install.stdout + install.stderr)
    assert install.returncode == 0 and b'ERROR:' not in install.stdout + install.stderr, install
    for cpus in (('1', '4') if args.cpus == 'both' else (args.cpus,)):
        path = '/nftables-' + cpus
        process = subprocess.run([*base, '--cpus', cpus, '--exec-file', path + '=' + str(FIXTURE),
                                  '--guest', path], capture_output=True, timeout=180)
        output = process.stdout + process.stderr
        (OUT / ('filter-' + cpus + '.log')).write_bytes(output)
        passed = process.returncode == 0 and marker in output
        results['checks'].append({'cpus': int(cpus), 'exit_code': process.returncode, 'passed': passed,
                                  'output': output.decode(errors='replace')})
        (OUT / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        assert passed, output[-12000:]
        print(f'{cpus}-CPU original iptables-nft/IP6 and actual filter: passed', flush=True)
print(f'Evidence: {OUT}')
