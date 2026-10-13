#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""One alternating ARM64 old/new batch for interpreter memory and real startup."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('nonjit', ROOT / 'tests/performance/nonjit.py')
nonjit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(nonjit)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def metrics(stderr):
    lines = stderr.decode().splitlines()
    assert len(lines) == 2, stderr[-4000:]
    native, runtime = [json.loads(line.split(': ', 1)[1]) for line in lines]
    return dict(native=native, runtime=runtime)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--baseline-commit', required=True)
    parser.add_argument('--vim-store', type=Path, required=True)
    parser.add_argument('--python-store', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    candidate = ROOT / 'target/aarch64-unknown-linux-musl/release/trames-run'
    libc = ROOT / 'target/musl-hybrid/libc.so'
    fixture = ROOT / 'tests/fixtures/bin/cpu-memory'
    qemu = os.getenv('QEMU_AARCH64', 'qemu-aarch64')
    rows = []
    with tempfile.TemporaryDirectory(prefix='trames-cpu-memory-') as directory:
        tmp = Path(directory)
        for name in ('cpu-memory', 'vim-default', 'python-imports'):
            modes = {}
            for sample in range(3):
                ordered = [('previous', args.baseline), ('current', candidate)]
                if sample % 2:
                    ordered.reverse()
                for mode, binary in ordered:
                    command = [qemu, '-cpu', 'max', str(binary), '--network', 'off',
                               '--accelerator', 'pure', '--metrics',
                               '--max-instructions', '100000000000', '--env', 'HOME=/root',
                               '--env', 'TERM=xterm-256color', '--env', 'PYTHONHASHSEED=0']
                    if name == 'cpu-memory':
                        command += ['--memory', str(fixture)]
                    else:
                        store = tmp / 'store'
                        shutil.copytree(args.vim_store if name == 'vim-default' else args.python_store, store)
                        command += ['--storage', str(store), '--musl-libc', str(libc)]
                    if name == 'vim-default':
                        command += ['--pty', '--guest', '/usr/bin/vim', '/tmp/performance-empty.txt']
                        seconds, profile = nonjit.vim(command, metrics)
                        endpoint = 'spawn to first visible cursor; normal startup, terminal replies, then :qa!'
                    else:
                        if name == 'python-imports':
                            command += ['--guest', '/usr/bin/python3', '-c', nonjit.PYTHON]
                        start = time.monotonic()
                        result = subprocess.run(command, capture_output=True, timeout=300)
                        expected = b'cpu-memory-ok\n' if name == 'cpu-memory' else b'python: 42\n'
                        assert (result.returncode, result.stdout) == (0, expected), (name, mode, result)
                        elapsed = time.monotonic() - start
                        profile = metrics(result.stderr)
                        seconds = elapsed if name == 'python-imports' else profile['runtime']['seconds']
                        endpoint = 'guest execution through exit' if name == 'cpu-memory' else 'spawn through exit/final sync'
                    assert profile['native']['faults'] == profile['native']['rejected'] == 0, profile
                    modes.setdefault(mode, {'samples': []})['samples'].append(dict(seconds=seconds, **profile))
                    print(name, mode, sample + 1, round(seconds, 4), flush=True)
                    if name != 'cpu-memory':
                        shutil.rmtree(store)
            for values in modes.values():
                values['median_seconds'] = statistics.median(row['seconds'] for row in values['samples'])
            if name == 'cpu-memory':
                assert len({row['runtime']['instructions'] for values in modes.values()
                            for row in values['samples']}) == 1, modes
            rows.append(dict(workload=name, endpoint=endpoint, modes=modes,
                             speedup=modes['previous']['median_seconds'] / modes['current']['median_seconds']))
    report = dict(scope='Actual ARM64 runtime under pinned QEMU on shared x86_64 Linux; '
                       'three alternating old/new samples, identical libc/programs and fresh copied stores. '
                       'Warm host caches, fixed PYTHONHASHSEED=0. No physical iPad or general speed claim.',
                  baseline_commit=args.baseline_commit, host=platform.platform(),
                  baseline_sha256=digest(args.baseline), candidate_sha256=digest(candidate),
                  libc_sha256=digest(libc), fixture_sha256=digest(fixture),
                  vim_manifest_sha256=digest(args.vim_store / 'manifest'),
                  python_manifest_sha256=digest(args.python_store / 'manifest'),
                  python_source_sha256=hashlib.sha256(nonjit.PYTHON.encode()).hexdigest(), rows=rows)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print([(row['workload'], round(row['speedup'], 3)) for row in rows], flush=True)


if __name__ == '__main__':
    main()
