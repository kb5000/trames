#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared syslog semantics and worker routing; never reads the host kernel log."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner', type=Path, required=True)
p.add_argument('--baseline', type=Path)
p.add_argument('--fixture', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
report = dict(passed=False, fixture_sha256=sha(a.fixture), runner_sha256=sha(a.runner),
              baseline_sha256=sha(a.baseline) if a.baseline else None, rows=[])
expected = b'worker-syslog queries=768: shared records, clear, faults, authority and seccomp: ok\n'
try:
    for version, runner in ([('before', a.baseline)] if a.baseline else []) + [('after', a.runner)]:
        for backend in (['memory'] if version == 'before' else ['memory', 'disk']):
            for cpus in (1, 2, 4):
                label = f'{version}-{backend}-{cpus}'
                with tempfile.TemporaryDirectory(dir=a.output, prefix=label) as directory:
                    storage = ['--memory'] if backend == 'memory' else ['--storage', directory]
                    command = [str(runner.resolve()), *storage, '--cpus', str(cpus), '--network', 'off',
                               '--metrics', '--max-instructions', '1000000000',
                               '--exec-file', f'/worker-syslog={a.fixture.resolve()}', '--guest', '/worker-syslog']
                    result = subprocess.run(command, capture_output=True, timeout=60)
                (a.output / f'{label}.stdout').write_bytes(result.stdout)
                (a.output / f'{label}.stderr').write_bytes(result.stderr)
                assert (result.returncode, result.stdout) == (0, expected), (label, result.returncode, result.stdout, result.stderr[-3000:])
                metrics = json.loads(next(line.split(': ', 1)[1] for line in result.stderr.decode().splitlines()
                                          if line.startswith('trames metrics: ')))
                assert metrics['runtime_error'] is None, (label, metrics)
                # Thread creation/exit and deliberately denied operations still join.
                # The shared queries bypass services. A thread may finish first,
                # leaving the other on the solo interpreter path, so not every
                # query necessarily contributes to the worker counter.
                if version == 'after' and cpus > 1:
                    assert metrics['worker_syscalls'] > 0, (label, metrics)
                    assert metrics['parallel_rounds'] < 32, (label, metrics)
                    assert metrics['service_requests'] == 0, (label, metrics)
                report['rows'].append(dict(case=label, metrics=metrics))
                print(label, 'PASS', 'rounds=', metrics['parallel_rounds'],
                      'worker_syscalls=', metrics['worker_syscalls'], flush=True)
    report['passed'] = True
finally:
    (a.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
