#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise guest log waits without accessing the host's kernel log."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner', type=Path, required=True)
p.add_argument('--fixture', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
report = dict(passed=False, runner_sha256=sha(a.runner), fixture_sha256=sha(a.fixture), rows=[])
try:
    for backend, cpus, legacy in [(backend, cpus, False) for backend in ('memory', 'disk') for cpus in (1, 2, 4)] + [('memory', 2, True)]:
        label = f'{backend}-{cpus}' + ('-no-epoll' if legacy else '')
        with tempfile.TemporaryDirectory(dir=a.output, prefix=label) as directory:
            storage = ['--memory'] if backend == 'memory' else ['--storage', directory]
            cmd = [str(a.runner.resolve()), *storage, '--cpus', str(cpus), '--network', 'off', '--metrics',
                   '--max-instructions', '1000000000', '--exec-file', f'/worker-log-wait={a.fixture.resolve()}',
                   '--guest', '/worker-log-wait', *(['no-epoll'] if legacy else [])]
            q = subprocess.run(cmd, capture_output=True, timeout=60)
        (a.output / f'{label}.stdout').write_bytes(q.stdout)
        (a.output / f'{label}.stderr').write_bytes(q.stderr)
        expected = f'worker-log-wait modes={3 if legacy else 4}: legacy stream, kmsg, poll, edge epoll and fork cursors: ok\n'.encode()
        assert (q.returncode, q.stdout) == (0, expected), (label, q.returncode, q.stdout, q.stderr[-4000:])
        metrics = json.loads(next(line.split(': ', 1)[1] for line in q.stderr.decode().splitlines()
                                  if line.startswith('trames metrics: ')))
        assert metrics['runtime_error'] is None
        if cpus > 1:
            assert metrics['worker_io_waits'] > 0 and metrics['worker_direct_syscalls'] > 0, metrics
            assert metrics['service_requests'] == 0, metrics
        report['rows'].append(dict(case=label, metrics=metrics))
        print(label, 'PASS', 'rounds=', metrics['parallel_rounds'], 'waits=', metrics['worker_io_waits'], flush=True)
    report['passed'] = True
finally:
    (a.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
