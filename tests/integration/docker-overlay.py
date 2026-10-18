#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Original signed guest Docker overlay2: probe/import/copy-up/diff/commit/isolation/restart.

No host Docker engine, socket, mount or container is used. Network behavior has
separate acceptance; these containers use --network none and normal isolation.
"""
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import m6

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'target/docker-overlay'
OUT.mkdir(parents=True, exist_ok=True)
RUNNER = os.getenv('TRAMES_RUNNER', str(ROOT / 'target/release/trames-run'))
SHELL = ROOT / 'tests/integration/docker-overlay.sh'
results = {'scope': __doc__, 'runner_sha256': hashlib.sha256(Path(RUNNER).read_bytes()).hexdigest(), 'shell_sha256': hashlib.sha256(SHELL.read_bytes()).hexdigest(), 'checks': []}
with tempfile.TemporaryDirectory(prefix='trames-docker-overlay-') as directory:
    tmp = Path(directory)
    m6.repository(tmp / 'repository', ('docker-engine', 'docker-cli', 'crun', 'libssl3'))
    packages = [p for p in sorted((tmp / 'repository').rglob('*.apk')) if not p.name.startswith(('dash-', 'yash-'))]
    results['packages_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in packages}
    rootfs = m6.CACHE / 'alpine-minirootfs-3.22.6-aarch64.tar.gz'
    results['rootfs_sha256'] = hashlib.sha256(rootfs.read_bytes()).hexdigest()
    with gzip.open(rootfs, 'rb') as source, (tmp / 'alpine.tar').open('wb') as dest:
        shutil.copyfileobj(source, dest)
    store = Path(os.getenv('TRAMES_OVERLAY_STORE', str(tmp / 'store')))
    base = [RUNNER, '--storage', str(store), '--network', 'full', '--cpus', '4',
            '--env', 'GOMAXPROCS=1', '--max-instructions', '30000000000']
    imported = [arg for p in packages for arg in ('--file', '/tmp/' + p.name + '=' + str(p))]
    if not (store / 'manifest').exists():
        install = subprocess.run([*base, '--rootfs', str(rootfs), *imported,
            '--file', '/tmp/alpine-rootfs.tar=' + str(tmp / 'alpine.tar'), '--guest', '/sbin/apk',
            '--repositories-file', '/dev/null', '--no-progress', 'add', *['/tmp/' + p.name for p in packages]],
            capture_output=True, timeout=300)
        (OUT / 'install.log').write_bytes(install.stdout + install.stderr)
        assert install.returncode == 0 and b'ERROR:' not in install.stdout + install.stderr, install
    started = time.monotonic()
    with (OUT / 'overlay.log').open('wb') as log:
        process = subprocess.run([*base, '--guest', '/bin/sh', '-ec', SHELL.read_text()],
            stdout=log, stderr=subprocess.STDOUT, timeout=900)
    output = (OUT / 'overlay.log').read_text()
    names = ('PROBE', 'COPY_UP', 'COMMIT', 'ISOLATION', 'RESTART', 'CLEANUP')
    checks = {n: ('\nDOCKER_OVERLAY_' + n + '_PASS\n') in output for n in names}
    results['checks'].append({'cpus': 4, 'exit_code': process.returncode,
        'seconds': round(time.monotonic() - started, 3), 'checks': checks, 'output': output})
    (OUT / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    assert process.returncode == 0 and all(checks.values()), output[-16000:]
    print('Original Docker overlay2: passed', flush=True)
print(f'Evidence: {OUT}')
