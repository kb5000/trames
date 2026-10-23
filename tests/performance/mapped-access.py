#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare fixed mapped loads/stores; diagnostic timing, never a CI score gate.

Build tests/fixtures/mapped-access-bench.c with native cc and static AArch64 musl
at -O2, then pass both binaries. Runner processes execute serially; alternate
before/after order. This isolates mapped-byte overhead, not whole-product speed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner', type=Path, required=True)
p.add_argument('--baseline', type=Path, required=True)
p.add_argument('--fixture', type=Path, required=True)
p.add_argument('--oracle', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--repeats', type=int, default=3)
p.add_argument('--host-cpu', type=int, help='Optional Linux host affinity for both runners')
a = p.parse_args()
if a.repeats < 1:
    p.error('--repeats must be positive')
if a.host_cpu is not None:
    if not hasattr(os, 'sched_setaffinity'):
        p.error('--host-cpu requires host sched_setaffinity')
    os.sched_setaffinity(0, {a.host_cpu})
a.output.mkdir(parents=True, exist_ok=False)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
report = dict(passed=False, scope=__doc__, host_cpu=a.host_cpu,
              sha256={k: sha(getattr(a, k)) for k in ('runner', 'baseline', 'fixture', 'oracle')},
              rows=[])
expected = b'mapped-loop: checksum and backing: ok\n'
try:
    for mode in ('file', 'memfd'):
        native = subprocess.run([str(a.oracle.resolve()), mode], capture_output=True, timeout=15)
        assert (native.returncode, native.stdout, native.stderr) == (0, expected, b''), native
        for trial in range(a.repeats):
            order = ('before', 'after') if trial % 2 == 0 else ('after', 'before')
            for version in order:
                binary = a.baseline if version == 'before' else a.runner
                label = f'{mode}-{trial}-{version}'
                command = [str(binary.resolve()), '--memory', '--network', 'off', '--cpus', '1',
                           '--max-instructions', '200000000', '--metrics',
                           str(a.fixture.resolve()), mode]
                result = subprocess.run(command, capture_output=True, timeout=60)
                (a.output / (label + '.stdout')).write_bytes(result.stdout)
                (a.output / (label + '.stderr')).write_bytes(result.stderr)
                assert (result.returncode, result.stdout) == (0, expected), (label, result)
                metrics = json.loads(next(line.removeprefix('trames metrics: ')
                    for line in result.stderr.decode().splitlines()
                    if line.startswith('trames metrics: ')))
                assert metrics['runtime_error'] is None, metrics
                row = dict(mode=mode, trial=trial, version=version,
                           cpu_seconds=metrics['cpu_seconds'], seconds=metrics['seconds'],
                           instructions=metrics['instructions'])
                report['rows'].append(row)
                print(label, row['cpu_seconds'], flush=True)
    report['summary'] = {}
    for mode in ('file', 'memfd'):
        medians = {v: statistics.median(row['cpu_seconds'] for row in report['rows']
                   if row['mode'] == mode and row['version'] == v) for v in ('before', 'after')}
        report['summary'][mode] = dict(median_cpu_seconds=medians,
                                      after_over_before=medians['after'] / medians['before'])
    report['passed'] = True
finally:
    (a.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
