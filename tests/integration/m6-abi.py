#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""AArch64 epoll/eventfd/timerfd/Unix IPC and record locks against QEMU."""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
fixture = ROOT / 'tests/fixtures/bin/m6'
expected = subprocess.run([os.getenv('QEMU_AARCH64', 'qemu-aarch64'), str(fixture)], capture_output=True, timeout=30)
assert expected.returncode == 0 and not expected.stderr, (expected.returncode, expected.stdout, expected.stderr)
runner = os.getenv('TRAMES_RUNNER', str(ROOT / 'target/release/trames-run'))
actual = subprocess.run([runner, '--memory', '--env', 'TRAMES_REQUIRE_EPOLL_PWAIT2=1', '--max-instructions', '100000000', '--trace-syscalls', str(fixture)], capture_output=True, timeout=60)
assert actual.returncode == 0 and actual.stdout == expected.stdout, (actual.returncode, actual.stdout, actual.stderr[-6000:])
assert b'syscall 441' in actual.stderr and b' = -38' not in actual.stderr, actual.stderr[-6000:]
print(actual.stdout.decode(), end='')
print('QEMU comparisons passed; epoll_pwait2 independently required (QEMU has no entry)')
