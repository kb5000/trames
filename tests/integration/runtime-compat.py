#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real musl altstack, fences, task I/O priority and enforced file-size limits."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'tests/fixtures/bin/runtime-compat'
RUNNER = os.getenv('TRAMES_RUNNER', str(ROOT / 'target/debug/trames-run'))
QEMU = os.getenv('QEMU_AARCH64', 'qemu-aarch64')
with tempfile.TemporaryDirectory(prefix='trames-runtime-compat-') as directory:
    oracle_env = dict(os.environ, TRAMES_ORACLE_SKIP_EXEC='1')
    oracle = subprocess.run([QEMU, '-cpu', 'max', str(FIXTURE)], cwd=directory, env=oracle_env, capture_output=True, timeout=60)
    assert (oracle.returncode, oracle.stdout, oracle.stderr) == (0, b'runtime-compat-ok\n', b''), oracle
    native = subprocess.run([str(FIXTURE)+'-host', 'autodisarm'], cwd=directory, env=oracle_env, capture_output=True, timeout=60)
    assert (native.returncode, native.stdout, native.stderr) == (0, oracle.stdout, b''), native
    for store in (['--memory'], ['--storage', directory+'/store']):
        for mode in ([], ['autodisarm']):
            selected = store if store[0] == "--memory" else [store[0], store[1]+("-auto" if mode else "-normal")]
            actual = subprocess.run([RUNNER, *selected, '--exec-file', '/bin/runtime-compat='+str(FIXTURE), str(FIXTURE), *mode], capture_output=True, timeout=60)
            assert (actual.returncode, actual.stdout, actual.stderr) == (0, oracle.stdout, b''), actual
        print('PASS', store[0], 'altstack/nesting/autodisarm/fork/thread/exec, membarrier, ioprio_get, RLIMIT_FSIZE/partial write/SIGXFSZ')
