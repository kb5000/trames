#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Named Unix IPC and Go netpoll primitives against independent native Linux."""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
fixture = ROOT/'tests/fixtures/bin/unix-sockets'
expected = subprocess.run([str(fixture.with_name('unix-sockets-host'))], capture_output=True, timeout=30)
assert expected.returncode == 0 and not expected.stderr, expected
runner = os.getenv('TRAMES_RUNNER', str(ROOT/'target/release/trames-run'))
for cpus in ('1', '4'):
    actual = subprocess.run([runner, '--memory', '--cpus', cpus, '--max-instructions', '100000000', str(fixture)], capture_output=True, timeout=30)
    assert actual.returncode == 0 and actual.stdout == expected.stdout, (cpus, actual.returncode, actual.stdout, actual.stderr[-6000:])
    print(f'{cpus}-CPU Unix IPC / ET / xattr / FIFO outcomes match native Linux', flush=True)
print(expected.stdout.decode(), end='')

fixture = fixture.with_name('pdeathsig')
expected = subprocess.run([str(fixture.with_name('pdeathsig-host'))], capture_output=True, timeout=30, check=True)
for cpus in ('1', '4'):
    actual = subprocess.run([runner, '--memory', '--cpus', cpus, '--max-instructions', '100000000', '--exec-file', '/pdeathsig='+str(fixture), '--guest', '/pdeathsig'], capture_output=True, timeout=30)
    assert actual.returncode == 0 and actual.stdout == expected.stdout, (cpus, actual.returncode, actual.stdout, actual.stderr)
    print(f'{cpus}-CPU parent death signal outcomes match native Linux', flush=True)
print(expected.stdout.decode(), end='')
