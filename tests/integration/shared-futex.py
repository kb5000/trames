#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check real musl futex queues with concurrent native memory work; no timing claims."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner', type=Path, required=True)
p.add_argument('--oracle', type=Path, required=True)
p.add_argument('--fixture', type=Path, default=Path('tests/fixtures/bin/shared-futex'))
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
report = dict(scope='Functional musl WAIT/WAKE_BITSET/CMP_REQUEUE, timeout/error and '
                   'shared-fork checks, overlapping native fill. Same guest ELF at '
                   '1/2/4 CPUs, host C output oracle; not a speed comparison.',
              host_arch=platform.machine(), runner_sha256=digest(a.runner),
              oracle_sha256=digest(a.oracle), fixture_sha256=digest(a.fixture), rows=[])
try:
    oracle = subprocess.run([str(a.oracle.resolve())], capture_output=True, timeout=120)
    (a.output / 'oracle.stdout').write_bytes(oracle.stdout)
    (a.output / 'oracle.stderr').write_bytes(oracle.stderr)
    assert oracle.returncode == 0 and not oracle.stderr, oracle
    for cpus in (1, 2, 4):
        cmd = [str(a.runner.resolve()), '--memory', '--network', 'off', '--cpus', str(cpus),
               '--accelerator', 'pure', '--metrics', '--max-instructions', '2000000000',
               str(a.fixture.resolve())]
        r = subprocess.run(cmd, capture_output=True, timeout=180)
        label = str(cpus)
        (a.output / (label + '.stdout')).write_bytes(r.stdout)
        (a.output / (label + '.stderr')).write_bytes(r.stderr)
        assert (r.returncode, r.stdout) == (0, oracle.stdout), (label, r.returncode, r.stdout, r.stderr[-6000:])
        values = dict(line.split(': ', 1) for line in r.stderr.decode().splitlines()
                      if line.startswith('trames '))
        native = json.loads(values['trames native metrics'])
        runtime = json.loads(values['trames metrics'])
        assert native['faults'] == native['rejected'] == native['fallbacks'] == 0, native
        assert native['function_calls'][2] > 0, native
        assert runtime['service_requests'] == 0, runtime
        if cpus > 1:
            assert runtime['worker_futex_wakes'] > 0, runtime
            assert runtime['worker_futex_woken'] > 0, runtime
        report['rows'].append(dict(cpus=cpus, runtime=runtime, native=native))
        print(label, 'CPU PASS', 'worker wakes', runtime['worker_futex_wakes'],
              'tickets', runtime['worker_futex_woken'], 'RPC', runtime['service_requests'], flush=True)
finally:
    (a.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
