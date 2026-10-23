#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check guest UTS concurrency and namespace lifetime, without changing host UTS."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner', type=Path, required=True)
p.add_argument('--fixtures', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
report = dict(passed=False, runner_sha256=sha(a.runner), rows=[],
              fixture_sha256={name: sha(a.fixtures / name) for name in ('worker-uts', 'namespaces')})
try:
    for fixture in ('worker-uts', 'namespaces'):
        for backend in ('memory', 'disk'):
            for cpus in (1, 2, 4):
                label = f'{fixture}-{backend}-{cpus}'
                with tempfile.TemporaryDirectory(dir=a.output, prefix=label) as directory:
                    storage = ['--memory'] if backend == 'memory' else ['--storage', directory]
                    command = [str(a.runner.resolve()), *storage, '--cpus', str(cpus), '--network', 'off', '--metrics',
                               '--max-instructions', '1000000000', '--exec-file', f'/{fixture}={(a.fixtures / fixture).resolve()}', '--guest', f'/{fixture}']
                    q = subprocess.run(command, capture_output=True, timeout=60)
                (a.output / f'{label}.stdout').write_bytes(q.stdout)
                (a.output / f'{label}.stderr').write_bytes(q.stderr)
                assert q.returncode == 0, (label, q.returncode, q.stdout, q.stderr[-4000:])
                if fixture == 'worker-uts':
                    assert q.stdout == b'worker-uts operations=1024: concurrent names, faults and authority: ok\n', q.stdout
                else:
                    assert b'chroot: independent fs root, absolute symlinks, dotdot, exec, privilege: ok\n' in q.stdout, q.stdout
                metrics = json.loads(next(line.split(': ', 1)[1] for line in q.stderr.decode().splitlines()
                                          if line.startswith('trames metrics: ')))
                assert metrics['runtime_error'] is None
                if fixture == 'worker-uts' and cpus > 1:
                    assert metrics['worker_syscalls'] > 0 and metrics['parallel_rounds'] < 32, metrics
                    assert metrics['service_requests'] == 0, metrics
                report['rows'].append(dict(case=label, metrics=metrics))
                print(label, 'PASS', 'rounds=', metrics['parallel_rounds'], flush=True)
    report['passed'] = True
finally:
    (a.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
