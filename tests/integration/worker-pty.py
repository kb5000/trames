#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Native oracle and identical ELF before/after; PTY bytes/waits, structural counts."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner', type=Path, required=True)
p.add_argument('--baseline', type=Path)
p.add_argument('--oracle', type=Path, required=True)
p.add_argument('--fixture', type=Path, default=Path('tests/fixtures/bin/worker-pty'))
p.add_argument('--output', type=Path, required=True)
p.add_argument('--reuse-oracles', type=Path, help='reuse verified native output for an identical fixture/oracle')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
report = dict(scope=__doc__, runner_sha256=sha(a.runner),
              baseline_sha256=sha(a.baseline) if a.baseline else None,
              fixture_sha256=sha(a.fixture), oracle_sha256=sha(a.oracle), passed=False, rows=[])
if a.reuse_oracles:
    previous = json.loads((a.reuse_oracles / 'summary.json').read_text())
    assert previous['passed'] and all(previous[k] == report[k] for k in ('fixture_sha256', 'oracle_sha256'))
    report['oracles_reused_from'] = str(a.reuse_oracles)
try:
    for mode in ('read', 'vector', 'epoll', 'poll'):
        for affinity, counts in (('spread', (1, 2, 4)), ('pinned', (4,))):
            oracle_path = f'oracle-{mode}-{affinity}.stdout'
            if a.reuse_oracles:
                expected = (a.reuse_oracles / oracle_path).read_bytes()
            else:
                oracle = subprocess.run([str(a.oracle.resolve()), mode, affinity], capture_output=True, timeout=30)
                (a.output / f'oracle-{mode}-{affinity}.stderr').write_bytes(oracle.stderr)
                assert oracle.returncode == 0 and not oracle.stderr, oracle
                expected = oracle.stdout
            (a.output / oracle_path).write_bytes(expected)
            for cpus in counts:
                versions = [('after', a.runner)]
                if a.baseline:
                    versions.insert(0, ('before', a.baseline))
                for version, runner in versions:
                    label = f'{mode}-{affinity}-{cpus}-{version}'
                    cmd = [str(runner.resolve()), '--memory', '--network', 'off', '--cpus', str(cpus),
                           '--accelerator', 'pure', '--metrics', '--max-instructions', '2000000000',
                           str(a.fixture.resolve()), mode, affinity]
                    result = subprocess.run(cmd, capture_output=True, timeout=60)
                    (a.output / f'{label}.stdout').write_bytes(result.stdout)
                    (a.output / f'{label}.stderr').write_bytes(result.stderr)
                    assert (result.returncode, result.stdout) == (0, expected), (label, result.returncode, result.stdout, result.stderr[-6000:])
                    values = dict(line.split(': ', 1) for line in result.stderr.decode().splitlines() if line.startswith('trames '))
                    runtime = json.loads(values['trames metrics'])
                    native = json.loads(values['trames native metrics'])
                    assert runtime['runtime_error'] is None and runtime['service_requests'] == 0, runtime
                    assert native['faults'] == native['fallbacks'] == native['rejected'] == 0, native
                    assert native['function_calls'][2] == 128, native
                    if version == 'after' and cpus > 1:
                        assert runtime['worker_direct_syscalls'] >= 128, runtime
                        assert runtime['worker_io_waits'] > 0, runtime
                        assert native['worker_calls'] > 0, native
                        if mode == 'poll':
                            assert runtime['worker_poll_syscalls'] > 0, runtime
                    report['rows'].append(dict(label=label, runtime=runtime, native=native))
                    print(label, 'rounds=', runtime['parallel_rounds'], 'direct=', runtime['worker_direct_syscalls'],
                          'waits=', runtime['worker_io_waits'], flush=True)
    report['passed'] = True
finally:
    (a.output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
