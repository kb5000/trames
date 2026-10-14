#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Identical C cases against native Linux execve/execveat and the ARM64 guest."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
fixtures = ROOT / 'tests/fixtures/bin'
runner = os.getenv('TRAMES_RUNNER', str(ROOT / 'target/release/trames-run'))
reference = subprocess.run([str(fixtures / 'fd-exec-host'), str(fixtures / 'fd-exec-child-host')], capture_output=True, timeout=30)
assert reference.returncode == 0 and not reference.stderr, (reference.returncode, reference.stdout, reference.stderr)
with tempfile.TemporaryDirectory(prefix='trames-fd-exec-') as folder:
    for cpus in ('1', '4'):
        store = Path(folder) / cpus
        actual = subprocess.run([runner, '--storage', str(store), '--cpus', cpus,
            '--rootfs', str(ROOT / 'target/alpine-download/alpine-minirootfs-3.22.6-aarch64.tar.gz'),
            '--exec-file', '/tmp/fd-child=' + str(fixtures / 'fd-exec-child'),
            '--max-instructions', '1000000000', str(fixtures / 'fd-exec'), '/tmp/fd-child'],
            capture_output=True, timeout=90)
        assert actual.returncode == 0 and actual.stdout == reference.stdout and not actual.stderr, (cpus, actual.returncode, actual.stdout, actual.stderr[-5000:])
        print(f'{cpus}-CPU ARM64 fd execution: {len(reference.stdout.splitlines())} cases match native Linux', flush=True)
print(reference.stdout.decode(), end='')
