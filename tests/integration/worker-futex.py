#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Same persistent-thread ELF before/after; native C output oracle, structural counts."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner', type=Path, required=True)
p.add_argument('--baseline', type=Path)
p.add_argument('--oracle', type=Path, required=True)
p.add_argument('--fixture', type=Path, default=Path('tests/fixtures/bin/worker-futex'))
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
report = dict(scope=__doc__, runner_sha256=sha(a.runner),
              baseline_sha256=sha(a.baseline) if a.baseline else None,
              fixture_sha256=sha(a.fixture), oracle_sha256=sha(a.oracle), passed=False, rows=[])
try:
    for mode in ('private', 'shared'):
        oracle = subprocess.run([str(a.oracle.resolve()), mode], capture_output=True, timeout=120)
        (a.output / f'oracle-{mode}.stdout').write_bytes(oracle.stdout)
        (a.output / f'oracle-{mode}.stderr').write_bytes(oracle.stderr)
        assert oracle.returncode == 0 and not oracle.stderr, oracle
        versions = [('after', a.runner)]
        if a.baseline:
            versions.insert(0, ('before', a.baseline))
        for cpus in (1, 2, 4):
            for version, runner in versions:
                label = f'{mode}-{cpus}-{version}'
                cmd = [str(runner.resolve()), '--memory', '--network', 'off', '--cpus', str(cpus),
                       '--accelerator', 'pure', '--metrics', '--max-instructions', '2000000000',
                       str(a.fixture.resolve()), mode]
                result = subprocess.run(cmd, capture_output=True, timeout=180)
                (a.output / f'{label}.stdout').write_bytes(result.stdout)
                (a.output / f'{label}.stderr').write_bytes(result.stderr)
                assert (result.returncode, result.stdout) == (0, oracle.stdout), (label, result.returncode, result.stdout, result.stderr[-6000:])
                values = dict(line.split(': ', 1) for line in result.stderr.decode().splitlines() if line.startswith('trames '))
                runtime = json.loads(values['trames metrics'])
                native = json.loads(values['trames native metrics'])
                assert runtime['runtime_error'] is None and runtime['service_requests'] == 0, runtime
                assert native['faults'] == native['fallbacks'] == native['rejected'] == 0, native
                assert native['function_calls'][2] > 0, native
                if version == 'after' and cpus > 1:
                    assert runtime['worker_futex_requeues'] >= 128, runtime
                    assert runtime['worker_futex_woken'] >= 128, runtime
                    assert runtime['worker_futex_waits'] > 0, runtime
                report['rows'].append(dict(mode=mode, cpus=cpus, version=version, runtime=runtime, native=native))
                print(label, 'PASS', 'epochs', runtime['parallel_rounds'],
                      'local requeues', runtime.get('worker_futex_requeues', 0), flush=True)
    report['passed'] = True
finally:
    (a.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
