#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check real static musl shared-resource I/O against host C output; no timing claims."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner', type=Path, required=True)
p.add_argument('--oracle', type=Path, required=True)
p.add_argument('--fixture', type=Path, default=Path('tests/fixtures/bin/epoch-services'))
p.add_argument('--output', type=Path, required=True)
p.add_argument('--iterations', type=int, default=128)
a = p.parse_args()
assert 1 <= a.iterations <= 8192
a.output.mkdir(parents=True, exist_ok=False)
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
report = dict(scope='Functional fixed-work native fill + pipe/eventfd scalar/vector I/O. '
                   'Two guest threads at each configured CPU count; host C output oracle, '
                   'not a CPU differential oracle or a performance comparison.',
              host_arch=platform.machine(), iterations=a.iterations,
              runner_sha256=digest(a.runner), oracle_sha256=digest(a.oracle),
              fixture_sha256=digest(a.fixture), rows=[])
try:
    for mode in ('pipe', 'eventfd'):
        oracle = subprocess.run([str(a.oracle.resolve()), mode, str(a.iterations)],
                                capture_output=True, timeout=120)
        assert oracle.returncode == 0 and not oracle.stderr, oracle
        (a.output / (mode + '-oracle.log')).write_bytes(oracle.stdout)
        for cpus in (1, 2, 4):
            cmd = [str(a.runner.resolve()), '--memory', '--network', 'off', '--cpus', str(cpus),
                   '--accelerator', 'pure', '--metrics', '--max-instructions', '2000000000',
                   str(a.fixture.resolve()), mode, str(a.iterations)]
            r = subprocess.run(cmd, capture_output=True, timeout=180)
            label = f'{mode}-{cpus}'
            (a.output / (label + '.stdout')).write_bytes(r.stdout)
            (a.output / (label + '.stderr')).write_bytes(r.stderr)
            assert (r.returncode, r.stdout) == (0, oracle.stdout), (label, r.returncode, r.stdout, r.stderr[-3000:])
            values = dict(line.split(': ', 1) for line in r.stderr.decode().splitlines()
                          if line.startswith('trames '))
            native = json.loads(values['trames native metrics'])
            runtime = json.loads(values['trames metrics'])
            assert native['faults'] == native['rejected'] == native['fallbacks'] == 0, native
            assert native['function_calls'][2] == a.iterations, native
            assert native['bytes_written'] == a.iterations * (1 << 20), native
            if cpus > 1:
                assert runtime['worker_direct_syscalls'] > 0, runtime
                assert runtime['service_requests'] == 0, runtime
            report['rows'].append(dict(mode=mode, cpus=cpus, native=native, runtime=runtime))
            print(label, 'PASS', 'direct', runtime['worker_direct_syscalls'],
                  'RPC', runtime['service_requests'], flush=True)
finally:
    (a.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
