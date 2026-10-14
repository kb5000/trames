#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Guest-created PTYs and sudo's IPC primitives against independent Linux/QEMU."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
fixture = ROOT / 'tests/fixtures/bin/pty'
native = subprocess.run([str(fixture.with_name('pty-host'))], capture_output=True, timeout=30)
assert native.returncode == 0 and not native.stderr, (native.returncode, native.stdout, native.stderr)
reference = subprocess.run([os.getenv('QEMU_AARCH64', 'qemu-aarch64'), '-E', 'TRAMES_QEMU_NO_PTLOCK_QUERY=1', str(fixture)], capture_output=True, timeout=30)
assert reference.returncode == 0 and not reference.stderr, (reference.returncode, reference.stdout, reference.stderr)
assert reference.stdout == native.stdout
runner = os.getenv('TRAMES_RUNNER', str(ROOT / 'target/release/trames-run'))
with tempfile.TemporaryDirectory(prefix='trames-pty-abi-') as folder:
    for cpus in ('1', '4'):
        actual = subprocess.run([runner, '--storage', str(Path(folder) / cpus), '--cpus', cpus,
            '--max-instructions', '1000000000', '--trace-syscalls', str(fixture)], capture_output=True, timeout=60)
        assert actual.returncode == 0 and actual.stdout == reference.stdout, (cpus, actual.returncode, actual.stdout, actual.stderr[-5000:])
        print(f'{cpus}-CPU PTY/IPC outcomes match independent Linux/QEMU', flush=True)
print(reference.stdout.decode(), end='')
