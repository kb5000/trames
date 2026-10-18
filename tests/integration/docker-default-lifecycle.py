#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Original default Docker exec or storage lifecycle in an independent guest.

Reuse separately verified bind/network/device coverage. No host Docker/mounts,
custom runtime/config, GOMAXPROCS, nofile or guest security/network overrides.
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import time
import m6

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('part', choices=('exec', 'storage'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / ('target/docker-default-' + args.part)
out.mkdir(parents=True, exist_ok=True)
runner = Path(os.getenv('TRAMES_RUNNER', root / 'target/release/trames-run'))
shell = root / 'tests/integration/docker-default-lifecycle.sh'
script = shell.read_text().replace('@PART@', args.part)
checks = ('DAEMON', 'PAYLOAD', 'CLEANUP') + (('EXEC',) if args.part == 'exec' else ('OVERLAY', 'RESTART'))
result = {'part': args.part, 'scope': __doc__, 'cpus': 4, 'inputs_sha256': {
    'runner': hashlib.sha256(runner.read_bytes()).hexdigest(),
    'shell': hashlib.sha256(shell.read_bytes()).hexdigest()}}
with tempfile.TemporaryDirectory(prefix='trames-default-' + args.part + '-') as directory:
    tmp = Path(directory)
    m6.repository(tmp / 'repository', ('docker-engine', 'docker-cli', 'libssl3'))
    packages = [p for p in sorted((tmp / 'repository').rglob('*.apk'))
                if not p.name.startswith(('dash-', 'yash-'))]
    assert any(p.name.startswith('runc-') for p in packages)
    assert not any(p.name.startswith('crun-') for p in packages)
    result['packages_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in packages}
    archive = m6.CACHE / 'alpine-minirootfs-3.22.6-aarch64.tar.gz'
    result['rootfs_sha256'] = hashlib.sha256(archive.read_bytes()).hexdigest()
    with gzip.open(archive, 'rb') as source, (tmp / 'alpine.tar').open('wb') as dest:
        shutil.copyfileobj(source, dest)
    base = [str(runner), '--storage', str(tmp / 'store'), '--network', 'full', '--cpus', '4',
            '--max-instructions', '50000000000']
    imported = [arg for p in packages for arg in ('--file', '/tmp/' + p.name + '=' + str(p))]
    install = subprocess.run([*base, '--rootfs', str(archive), *imported,
        '--file', '/tmp/alpine-rootfs.tar=' + str(tmp / 'alpine.tar'), '--guest', '/sbin/apk',
        '--repositories-file', '/dev/null', '--no-progress', 'add', *['/tmp/' + p.name for p in packages]],
        capture_output=True, timeout=300)
    (out / 'install.log').write_bytes(install.stdout + install.stderr)
    assert install.returncode == 0 and b'ERROR:' not in install.stdout + install.stderr
    started = time.monotonic()
    expired = threading.Event()
    with (out / 'lifecycle.log').open('wb') as log:
        process = subprocess.Popen([*base, '--guest', '/bin/sh', '-ec', script],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

        def timeout():
            expired.set()
            process.kill()

        timer = threading.Timer(2700, timeout)
        timer.daemon = True
        timer.start()
        try:
            for line in process.stdout:
                log.write(line)
                log.flush()
                print(line.decode(errors='replace'), end='', flush=True)
            code = process.wait()
        finally:
            timer.cancel()
            if process.poll() is None:
                process.kill()
                process.wait()
    output = (out / 'lifecycle.log').read_text()
    result.update(exit_code=code, seconds=round(time.monotonic() - started, 3), timed_out=expired.is_set(),
                  checks={name: ('\nDOCKER_DEFAULT_' + name + '_PASS\n') in output for name in checks})
    (out / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
    assert code == 0 and all(result['checks'].values()) and not expired.is_set(), result
print('Original default Docker lifecycle passed:', args.part, flush=True)
