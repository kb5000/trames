#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Same ARM64 ELF before/after: live OFD state and worker-local FD controls."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import resource
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner', type=Path, required=True)
p.add_argument('--baseline', type=Path)
p.add_argument('--fixture', type=Path)
p.add_argument('--kind', choices=('status', 'table', 'locks'), default='status')
p.add_argument('--oracle', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--cpus', type=int, nargs='+', choices=(1, 2, 4), default=[1, 2, 4])
p.add_argument('--repeats', type=int, default=1, help='paired samples for an isolated timing comparison')
a = p.parse_args()
if a.fixture is None:
    a.fixture = Path('tests/fixtures/bin/' + ('worker-record-locks' if a.kind == 'locks' else 'worker-fd-' + a.kind))
if a.repeats < 1:
    p.error('--repeats must be positive')
a.output.mkdir(parents=True, exist_ok=False)
marker = b'worker-record-locks' if a.kind == 'locks' else b'worker-fd-' + a.kind.encode()
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
report = dict(passed=False, fixture_sha256=sha(a.fixture), runner_sha256=sha(a.runner),
              baseline_sha256=sha(a.baseline) if a.baseline else None, oracle_sha256=sha(a.oracle), rows=[])

def run(label, command, domains=False):
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    start = time.monotonic()
    result = subprocess.run(command, capture_output=True, timeout=60)
    wall = time.monotonic() - start
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    (a.output / f'{label}.stdout').write_bytes(result.stdout)
    (a.output / f'{label}.stderr').write_bytes(result.stderr)
    assert result.returncode == 0, (label, result.returncode, result.stdout, result.stderr[-3000:])
    pattern = rb'expected-worker-fd: (\d+)\nfd-loop-seconds: ([\d.]+)\n' + marker + rb' PASS\n'
    match = re.fullmatch(pattern, result.stdout)
    assert (result.stdout == b'worker-fd-table-domains PASS\n') if domains else match, (label, result.stdout)
    row = dict(case=label, expected_worker_fd=None if domains else int(match[1]), guest_loop_seconds=None if domains else float(match[2]),
               host_wall_seconds=wall, host_child_cpu_seconds=after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime)
    if not domains:
        assert row['guest_loop_seconds'] > 0
    if not label.startswith('oracle'):
        metrics = json.loads(next(line.split(': ', 1)[1] for line in result.stderr.decode().splitlines() if line.startswith('trames metrics: ')))
        assert metrics['runtime_error'] is None
        row['metrics'] = metrics
    report['rows'].append(row)
    print(label, 'PASS', flush=True)
    return row

try:
    oracle = run('oracle', [str(a.oracle.resolve())] + ([str((a.output / 'native-lock-data').resolve())] if a.kind == 'locks' else []))
    for cpus in a.cpus:
        for sample in range(a.repeats):
            versions = ([('before', a.baseline)] if a.baseline else []) + [('after', a.runner)]
            if sample % 2:
                versions.reverse()
            for version, runner in versions:
                label = f'{version}-{cpus}' + (f'-{sample}' if a.repeats > 1 else '')
                command = [str(runner.resolve()), '--memory', '--cpus', str(cpus), '--metrics', '--max-instructions', '500000000']
                command += (['--exec-file', f'/fd-table-self={a.fixture.resolve()}', '--guest', '/fd-table-self']
                    if a.kind == 'table' else [str(a.fixture.resolve())])
                if a.kind == 'locks':
                    command += ['/record-lock-data']
                row = run(label, command)
                row.update(version=version, cpus=cpus, sample=sample)
                assert row['expected_worker_fd'] == oracle['expected_worker_fd']
                if version == 'after':
                    m = row['metrics']
                    assert m['worker_fd_syscalls'] == (row['expected_worker_fd'] if cpus > 1 else 0), (row['case'], m)
                    assert m['service_requests'] == 0, (row['case'], m)
                    if cpus > 1 and a.kind == 'status':
                        assert m['worker_direct_syscalls'] >= row['expected_worker_fd'] // 5, (row['case'], m)
    if a.kind == 'table':
        run('oracle-domains', [str(a.oracle.resolve()), 'domains'], domains=True)
        for cpus in a.cpus:
            run(f'domains-{cpus}', [str(a.runner.resolve()), '--memory', '--cpus', str(cpus), '--metrics',
                '--max-instructions', '500000000', '--exec-file', f'/fd-table-self={a.fixture.resolve()}',
                '--guest', '/fd-table-self', 'domains'], domains=True)
    report['passed'] = True
finally:
    (a.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
