#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare init-related ABI fixtures with native Linux, without booting host init."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
RUNNER = os.getenv('TRAMES_RUNNER', str(ROOT / 'target/release/trames-run'))
FIXTURES = Path(os.getenv('TRAMES_SYSTEMD_FIXTURES', ROOT / 'tests/fixtures/bin'))
with tempfile.TemporaryDirectory(prefix='trames-systemd-abi-') as directory:
    for name in ('ofd-locks', 'unix-notify', 'mount-lifecycle'):
        native = Path(directory) / name
        subprocess.run([os.getenv('CC', 'cc'), '-O2', '-Wall', '-Wextra', '-Werror',
                        '-pthread', str(ROOT / 'tests/fixtures' / f'{name}.c'),
                        '-o', str(native)], check=True)
        native_command = [str(native)]
        if name == 'mount-lifecycle' and os.geteuid() != 0:
            native_command = ['sudo', '-n', *native_command]
        oracle = subprocess.run(native_command, capture_output=True, timeout=30, check=True)
        assert not oracle.stderr, oracle.stderr
        for cpus in ('1', '4'):
            result = subprocess.run([
                RUNNER, '--memory', '--network', 'off', '--cpus', cpus,
                '--max-instructions', '500000000', '--exec-file',
                f'/{name}={FIXTURES / name}', '--guest', f'/{name}',
            ], capture_output=True, timeout=60)
            assert result.returncode == 0 and result.stdout == oracle.stdout, (
                name, cpus, result.returncode, result.stdout, result.stderr[-8000:])
            print(f'{cpus}-CPU {name}: matches native Linux', flush=True)
        print(oracle.stdout.decode(), end='')
