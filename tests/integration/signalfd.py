#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare signal descriptor semantics against native Linux (not host FD passthrough)."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
RUNNER = os.getenv('TRAMES_RUNNER', str(ROOT/'target/release/trames-run'))
with tempfile.TemporaryDirectory(prefix='trames-signalfd-') as directory:
    native = Path(directory)/'native'
    subprocess.run([os.getenv('CC', 'cc'), '-O2', '-pthread', '-Wall', '-Wextra', '-Werror',
                    str(ROOT/'tests/fixtures/signalfd.c'), '-o', str(native)], check=True)
    oracle = subprocess.run([str(native)], capture_output=True, timeout=30)
    assert oracle.returncode == 0 and not oracle.stderr, oracle
    for cpus in ('1', '4'):
        actual = subprocess.run([RUNNER, '--memory', '--cpus', cpus, '--network', 'off',
            '--max-instructions', '500000000', '--exec-file',
            '/bin/signalfd='+str(ROOT/'tests/fixtures/bin/signalfd'), '--guest', '/bin/signalfd'],
            capture_output=True, timeout=60)
        assert (actual.returncode, actual.stdout, actual.stderr) == (0, oracle.stdout, b''), (cpus, actual)
        print(f'{cpus}-CPU signalfd: {oracle.stdout.count(b": ok")} native Linux signal queue/read/poll/epoll/lifetime comparisons passed', flush=True)
