#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fresh signed Python: the owner's pass_fds lab and original libc fexecve."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('m6_repository', ROOT / 'tests/integration/m6.py')
m6 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m6)
runner = os.getenv('TRAMES_RUNNER', str(ROOT / 'target/release/trames-run'))
with tempfile.TemporaryDirectory(prefix='trames-memfd-python-') as folder:
    tmp = Path(folder)
    m6.repository(tmp / 'repository', ('python3',))
    packages = sorted((tmp / 'repository').rglob('*.apk'))
    base = [runner, '--storage', str(tmp / 'store'), '--cpus', '4', '--max-instructions', '10000000000']
    if os.getenv('TRAMES_TEST_MUSL'):
        base += ['--musl-libc', os.environ['TRAMES_TEST_MUSL'], '--accelerator', 'pure']
    files = []
    for package in packages:
        files += ['--file', '/tmp/' + package.name + '=' + str(package)]
    install = subprocess.run([*base, '--rootfs', str(m6.CACHE / 'alpine-minirootfs-3.22.6-aarch64.tar.gz'), *files,
        '--guest', '/sbin/apk', '--repositories-file', '/dev/null', '--no-progress', 'add',
        *['/tmp/' + p.name for p in packages]], capture_output=True, timeout=300)
    assert install.returncode == 0 and b'ERROR:' not in install.stdout + install.stderr, (install.stdout, install.stderr)
    result = subprocess.run([*base,
        '--exec-file', '/tmp/arm64lab/hello=' + str(ROOT / 'tests/fixtures/bin/fd-exec-child'),
        '--file', '/tmp/memfd_lab.py=' + str(ROOT / 'tests/fixtures/memfd-lab.py'),
        '--guest', '/usr/bin/python3', '/tmp/memfd_lab.py'], capture_output=True, timeout=120)
    assert result.returncode == 0 and not result.stderr, (result.returncode, result.stdout, result.stderr)
    assert result.stdout == (b'[PASS] Anonymous ELF execution!\n'
        b'[PASS] Python os.execve(fd) / libc fexecve!\n'
        b'[PASS] Closed fd returns ENOENT!\n'), result.stdout
    print(result.stdout.decode(), end='')
