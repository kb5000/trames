#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Diagnostic only: record where an unmodified signed guest crun stops.

This creates an OCI input bundle for the original runtime; it is not a custom
container launcher. A blocked run is recorded as such, never as acceptance.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import m6

ROOT = Path(__file__).resolve().parents[2]
RUNNER = os.getenv('TRAMES_RUNNER', str(ROOT/'target/release/trames-run'))
OUT = ROOT/'target/container-probe'
OUT.mkdir(parents=True, exist_ok=True)
config = {
    'ociVersion': '1.0.2', 'root': {'path': 'rootfs', 'readonly': False},
    'process': {'terminal': False, 'user': {'uid': 0, 'gid': 0},
        'args': ['/bin/true'], 'env': ['PATH=/bin'], 'cwd': '/', 'noNewPrivileges': True,
        'capabilities': {key: ['CAP_CHOWN', 'CAP_SETUID', 'CAP_SETGID']
                         for key in ('bounding', 'effective', 'permitted')}},
    'hostname': 'container', 'mounts': [],
    'linux': {'rootfsPropagation': 'private', 'namespaces': [{'type': key} for key in ('pid', 'mount', 'uts')]},
}
results = {'config': config, 'checks': []}
logs = []
with tempfile.TemporaryDirectory(prefix='trames-crun-probe-') as directory:
    tmp = Path(directory)
    m6.repository(tmp/'repository', ('crun', 'libcap-utils'))
    packages = sorted((tmp/'repository').rglob('*.apk'))
    results['packages_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in packages}
    base = [RUNNER, '--storage', str(tmp/'store'), '--network', 'off', '--cpus', '4', '--max-instructions', '2000000000']
    imported = []
    for package in packages:
        imported += ['--file', '/tmp/'+package.name+'='+str(package)]
    p = subprocess.run([*base, '--rootfs', str(m6.CACHE/'alpine-minirootfs-3.22.6-aarch64.tar.gz'),
        *imported, '--guest', '/sbin/apk', '--repositories-file', '/dev/null', '--no-progress',
        'add', *['/tmp/'+p.name for p in packages]], capture_output=True, timeout=180)
    assert p.returncode == 0 and b'ERROR:' not in p.stdout+p.stderr, p
    logs.append('=== signed package install ===\n'+(p.stdout+p.stderr).decode())
    cfg = tmp/'config.json'
    cfg.write_text(json.dumps(config, indent=2)+'\n')
    for label, command in (
        ('version', 'crun --version'), ('list', 'crun --root /run/crun list'),
        ('capsh', 'capsh --print'),
        ('run', 'mkdir -p /bundle/rootfs/dev /bundle/rootfs/proc /bundle/rootfs/sys /bundle/rootfs/tmp; cd /bundle; crun --debug --root /run/crun --cgroup-manager disabled run probe; status=$?; dmesg; exit "$status"'),
    ):
        extra = []
        if label == 'run':
            extra = ['--file', '/bundle/config.json='+str(cfg),
                '--exec-file', '/bundle/rootfs/bin/true='+str(ROOT/'target/busybox-build/busybox')]
        p = subprocess.run([*base, *extra, '--guest', '/bin/sh', '-c', command], capture_output=True, timeout=90)
        stdout, stderr = p.stdout.decode(), p.stderr.decode()
        results['checks'].append({'label': label, 'command': command, 'exit_code': p.returncode, 'stdout': stdout, 'stderr': stderr})
        logs.append(f'=== {label}; exit={p.returncode} ===\n{stdout}{stderr}')
        if label != 'run':
            assert p.returncode == 0, (label, p.returncode, stdout, stderr)
        else:
            results['minimal_run_completed'] = p.returncode == 0
results['scope'] = 'Exploratory original crun probe; even a successful /bin/true is not full container conformance.'
(OUT/'results.json').write_text(json.dumps(results, indent=2)+'\n')
(OUT/'probe.log').write_text('\n'.join(logs))
print('\n'.join(logs))
print('Minimal crun run: '+('completed' if results['minimal_run_completed'] else 'BLOCKED (see recorded exit and diagnostics)'))
print(f'Evidence: {OUT}')
