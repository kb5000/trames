#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare cohort lifetime policies with identical, oracle-checked fixed work.

The candidate may be a diagnostic build. Longer terminal/control latency is not
a production optimization; this estimates the value of event-driven cohorts.
Run after builds finish, without concurrent benchmarks. Never a CI speed gate.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
for name in ('baseline', 'candidate', 'fixture', 'oracle', 'output'):
    p.add_argument('--' + name, type=Path, required=True)
p.add_argument('--repeats', type=int, default=3)
p.add_argument('--host-cpus', help='Optional comma-separated Linux host affinity')
a = p.parse_args()
if a.repeats < 1:
    p.error('--repeats must be positive')
if a.host_cpus:
    os.sched_setaffinity(0, {int(n) for n in a.host_cpus.split(',')})
a.output.mkdir(parents=True, exist_ok=False)
sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
report = dict(passed=False, scope=__doc__, host_affinity=sorted(os.sched_getaffinity(0)),
              sha256={k: sha(getattr(a, k)) for k in ('baseline', 'candidate', 'fixture', 'oracle')},
              rows=[])
cases = [('compute', 2, 64, 65536), ('compute', 4, 64, 65536),
         ('compute', 16, 64, 65536), ('compute', 4, 0, 65536),
         ('compute', 4, 128, 65536), ('fd', 4, 64, 4096), ('vm', 4, 64, 2048)]
try:
    for mode, threads, fds, rounds in cases:
        key = f'{mode}-{threads}t-{fds}fd'
        args = [mode, str(threads), str(fds), str(rounds)]
        with tempfile.TemporaryDirectory(dir=a.output) as directory:
            oracle = subprocess.run([str(a.oracle.resolve()), *args], cwd=directory,
                                    capture_output=True, timeout=30)
        assert oracle.returncode == 0 and not oracle.stderr, oracle
        for trial in range(a.repeats):
            order = ('baseline', 'candidate') if trial % 2 == 0 else ('candidate', 'baseline')
            for version in order:
                label = f'{key}-{trial}-{version}'
                command = [str(getattr(a, version).resolve()), '--memory', '--network', 'off',
                           '--cpus', '4', '--pty', '--metrics', '--max-instructions', '4000000000',
                           str(a.fixture.resolve()), *args]
                result = subprocess.run(command, input=b'', capture_output=True, timeout=90)
                (a.output / (label + '.stdout')).write_bytes(result.stdout)
                (a.output / (label + '.stderr')).write_bytes(result.stderr)
                assert result.returncode == 0, (label, result)
                assert result.stdout.replace(b'\r\n', b'\n') == oracle.stdout, (label, result.stdout, oracle.stdout)
                metrics = json.loads(next(line.split(': ', 1)[1] for line in result.stderr.decode().splitlines()
                                          if line.startswith('trames metrics: ')))
                assert metrics['runtime_error'] is None and metrics['parallel_rounds'] > 0, metrics
                report['rows'].append(dict(case=key, trial=trial, version=version, metrics=metrics))
                print(label, round(metrics['seconds'], 4), 'seconds', metrics['parallel_rounds'], 'rounds', flush=True)
    report['summary'] = {}
    for mode, threads, fds, _ in cases:
        key = f'{mode}-{threads}t-{fds}fd'
        medians = {version: {metric: statistics.median(row['metrics'][metric] for row in report['rows']
                                                   if row['case'] == key and row['version'] == version)
                            for metric in ('seconds', 'parallel_rounds', 'dispatch_seconds', 'memory_seconds',
                                           'stop_seconds', 'service_requests', 'instructions')}
                   for version in ('baseline', 'candidate')}
        report['summary'][key] = dict(medians=medians,
                                     candidate_over_baseline=medians['candidate']['seconds'] / medians['baseline']['seconds'])
    report['passed'] = True
finally:
    (a.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
