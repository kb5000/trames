#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Watched-file worker I/O, blocking/edge waits and watch removal at 1/2/4 CPUs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import statistics
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner', type=Path, required=True)
p.add_argument('--baseline', type=Path)
p.add_argument('--oracle', type=Path, required=True)
p.add_argument('--fixture', type=Path, default=Path('tests/fixtures/bin/worker-inotify'))
p.add_argument('--output', type=Path, required=True)
p.add_argument('--repeats', type=int, default=3)
a = p.parse_args()
if a.repeats < 1:
    p.error('--repeats must be positive')
a.output.mkdir(parents=True, exist_ok=False)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
report = dict(passed=False, fixture_sha256=sha(a.fixture), runner_sha256=sha(a.runner),
              baseline_sha256=sha(a.baseline) if a.baseline else None,
              oracle_sha256=sha(a.oracle), rows=[])


def run(label, command):
    result = subprocess.run(command, capture_output=True, timeout=60)
    (a.output / f'{label}.stdout').write_bytes(result.stdout)
    (a.output / f'{label}.stderr').write_bytes(result.stderr)
    assert result.returncode == 0, (label, result.returncode, result.stderr[-3000:])
    match = re.fullmatch(rb'worker-inotify rounds=64 seconds=([\d.]+) PASS\n', result.stdout)
    assert match, (label, result.stdout)
    row = dict(case=label, seconds=float(match[1]))
    if label != 'oracle':
        metrics = json.loads(next(line.split(': ', 1)[1] for line in result.stderr.decode().splitlines()
                                  if line.startswith('trames metrics: ')))
        assert metrics['runtime_error'] is None
        row['metrics'] = metrics
    report['rows'].append(row)
    print(label, 'PASS', f"{row['seconds']:.6f}s", flush=True)
    return row


try:
    run('oracle', [str(a.oracle.resolve()), str((a.output / 'native-data').resolve())])
    for backend in ('memory', 'disk'):
        for cpus in (1, 2, 4):
            for sample in range(a.repeats if backend == 'memory' else 1):
                versions = ([('before', a.baseline)] if a.baseline and backend == 'memory' else []) + [('after', a.runner)]
                if sample % 2:
                    versions.reverse()
                for version, runner in versions:
                    label = f'{version}-{backend}-{cpus}-{sample}'
                    storage = ['--memory'] if backend == 'memory' else ['--storage', str(a.output / f'{label}-store')]
                    row = run(label, [str(runner.resolve()), *storage, '--cpus', str(cpus),
                        '--metrics', '--max-instructions', '500000000', str(a.fixture.resolve()),
                        '/notify-data'])
                    row.update(version=version, backend=backend, cpus=cpus, sample=sample)
                    if version == 'after' and cpus > 1:
                        metrics = row['metrics']
                        assert metrics['worker_file_syscalls'] >= 64, row
                        assert metrics['worker_poll_syscalls'] >= 32, row
                        assert metrics['worker_fd_syscalls'] >= 1, row
                        assert metrics['service_requests'] == 0, row
    report['medians'] = {f'{v}-{c}': statistics.median(r['seconds'] for r in report['rows']
                         if r.get('version') == v and r.get('cpus') == c and r.get('backend') == 'memory')
                         for v in (['before'] if a.baseline else []) + ['after'] for c in (1, 2, 4)}
    report['passed'] = True
finally:
    (a.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
