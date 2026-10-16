#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Configured committed-memory limits across allocation, threads, fork and exec."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
runner = os.getenv('TRAMES_RUNNER', str(ROOT / 'target/release/trames-run'))
fixture = ROOT / 'tests/fixtures/bin/memory-budget'
with tempfile.TemporaryDirectory(prefix='trames-memory-budget-') as directory:
    for cpus in ('1', '4'):
        result = subprocess.run([
            runner, '--storage', directory + '/store-' + cpus, '--cpus', cpus,
            '--memory-budget', '12MiB', '--process-memory-budget', '8MiB',
            '--exec-file', '/memory-budget=' + str(fixture), str(fixture)
        ], capture_output=True, timeout=90)
        assert (result.returncode, result.stdout, result.stderr) == (
            0, b'memory budget: reports, process quota, shared threads, fork aggregate and exec passed\n', b''
        ), result
        print(f'{cpus} CPU: configured memory budgets passed', flush=True)
    for total, process in [('0', '0'), ('1MiB', '2MiB'), ('4097', '4096')]:
        absent = directory + '/invalid-' + total
        result = subprocess.run([runner, '--storage', absent, '--memory-budget', total,
                                 '--process-memory-budget', process, str(fixture)], capture_output=True, timeout=10)
        assert result.returncode != 0 and b'memory budget' in result.stderr, result
        assert not Path(absent).exists(), 'invalid configuration must fail before creating the store'
    print('Invalid memory budgets fail before storage mutation', flush=True)
