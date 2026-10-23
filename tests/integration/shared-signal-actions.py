#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""CLONE_SIGHAND/exec/reset semantics and worker sigaction queries, with identical ARM64 ELF."""
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
p.add_argument('--fixture', type=Path, default=Path('tests/fixtures/bin/shared-signal-actions'))
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
    match = re.fullmatch(rb'shared-signal-actions queries=512 seconds=([\d.]+) PASS\n', result.stdout)
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


def guest(label, runner, cpus, storage, benchmark=False):
    return run(label, [str(runner.resolve()), *storage, '--cpus', str(cpus), '--metrics',
        '--max-instructions', '500000000', '--exec-file', f'/sighand-self={a.fixture.resolve()}',
        '--guest', '/sighand-self', *(['benchmark'] if benchmark else [])])


try:
    run('oracle', [str(a.oracle.resolve())])
    for cpus in (1, 2, 4):
        for backend in ('memory', 'disk'):
            label = f'domains-{backend}-{cpus}'
            storage = ['--memory'] if backend == 'memory' else ['--storage', str(a.output / f'{label}-store')]
            guest(label, a.runner, cpus, storage)
        for sample in range(a.repeats):
            versions = ([('before', a.baseline)] if a.baseline else []) + [('after', a.runner)]
            if sample % 2:
                versions.reverse()
            for version, runner in versions:
                row = guest(f'{version}-{cpus}-{sample}', runner, cpus, ['--memory'], True)
                row.update(version=version, cpus=cpus, sample=sample)
                if version == 'after' and cpus > 1:
                    assert row['metrics']['worker_syscalls'] >= 512, row
                    assert row['metrics']['service_requests'] == 0, row
                    assert row['metrics']['parallel_rounds'] < 16, row
    report['medians'] = {f'{v}-{c}': statistics.median(r['seconds'] for r in report['rows']
                         if r.get('version') == v and r.get('cpus') == c)
                         for v in (['before'] if a.baseline else []) + ['after'] for c in (1, 2, 4)}
    report['passed'] = True
finally:
    (a.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
