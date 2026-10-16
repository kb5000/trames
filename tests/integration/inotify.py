#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare guest inotify/epoll/fork with real Linux, for both VFS backends."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'tests/fixtures/bin/inotify'
RUNNER = os.getenv('TRAMES_RUNNER', str(ROOT / 'target/debug/trames-run'))
with tempfile.TemporaryDirectory(prefix='trames-inotify-') as directory:
    commands = [[str(FIXTURE)+'-host'], [os.getenv('QEMU_AARCH64', 'qemu-aarch64'), '-cpu', 'max', str(FIXTURE)]]
    for backend in (['--memory'], ['--storage', directory+'/store']):
        commands.append([RUNNER, *backend, '--exec-file', '/bin/inotify='+str(FIXTURE), str(FIXTURE)])
    for command in commands:
        result = subprocess.run(command, capture_output=True, timeout=60)
        assert (result.returncode, result.stdout, result.stderr) == (0, b'inotify-ok\n', b''), (command, result)
        print('PASS', command[:3])
