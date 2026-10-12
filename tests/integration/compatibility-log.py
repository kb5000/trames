#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real musl file ABI against QEMU; guest fault diagnostics through actual BusyBox dmesg."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'tests/fixtures/bin/compatibility-log'
BUSYBOX = ROOT / 'target/busybox-build/busybox'
RUNNER = os.getenv('TRAMES_RUNNER', str(ROOT / 'target/debug/trames-run'))
QEMU = os.getenv('QEMU_AARCH64', 'qemu-aarch64')
with tempfile.TemporaryDirectory(prefix='trames-compatibility-log-') as directory:
    oracle = subprocess.run([QEMU, '-cpu', 'max', str(FIXTURE)], cwd=directory, capture_output=True, timeout=30)
    assert (oracle.returncode, oracle.stdout, oracle.stderr) == (0, b'compatibility-abi-ok\n', b''), oracle
    # QEMU user rejects this Linux-valid CLONE_FILES-only clone. Use the native
    # Linux kernel for that case, and keep the ordinary ABI differential on ARM64.
    shared = subprocess.run([str(ROOT / 'tests/fixtures/bin/compatibility-log-host'), 'shared-fds'], cwd=directory, capture_output=True, timeout=30)
    assert (shared.returncode, shared.stdout, shared.stderr) == (0, b'shared-fds-ok\n', b''), shared
    for store in (['--memory'], ['--storage', directory + '/store']):
        actual = subprocess.run([RUNNER, *store, str(FIXTURE)], capture_output=True, timeout=30)
        assert (actual.returncode, actual.stdout, actual.stderr) == (oracle.returncode, oracle.stdout, oracle.stderr), actual
        actual = subprocess.run([RUNNER, *store, str(FIXTURE), 'shared-fds'], capture_output=True, timeout=30)
        assert (actual.returncode, actual.stdout, actual.stderr) == (shared.returncode, shared.stdout, shared.stderr), actual
        actual = subprocess.run([RUNNER, *store, '--exec-file', '/bin/busybox=' + str(BUSYBOX), str(FIXTURE), 'log'], capture_output=True, timeout=30)
        assert actual.returncode == 0 and actual.stderr == b'', actual
        assert b'dmesg-after-exec-marker' in actual.stdout and b'compatibility-log-ok\n' in actual.stdout, actual
        print('PASS', store[0], 'real musl statx/preadv/pwritev/close_range; child SVE fault, ENOSYS, kmsg, READ_CLEAR and BusyBox dmesg across exec')
