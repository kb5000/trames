#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""One fixed-work futex/native handoff comparison; no application speed claims."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline', type=Path, required=True)
p.add_argument('--runner', type=Path, required=True)
p.add_argument('--oracle', type=Path, required=True)
p.add_argument('--fixture', type=Path, default=Path('tests/fixtures/bin/task-handoff'))
p.add_argument('--output', type=Path, required=True)
p.add_argument('--rounds', type=int, default=2048)
a = p.parse_args()
assert 1 <= a.rounds <= 100000
a.output.mkdir(parents=True, exist_ok=False)
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
report = dict(scope='One sample per 1/2/4 CPU before/after, fixed musl ping-pong + '
                   '2*rounds native 4 KiB fills. Host C assertions/output oracle. '
                   'Synthetic scheduling comparison, not CoreMark or application performance.',
              host_arch=platform.machine(), rounds=a.rounds,
              binaries={k: digest(v) for k, v in [('baseline', a.baseline), ('runner', a.runner),
                                                ('oracle', a.oracle), ('fixture', a.fixture)]}, rows=[])
try:
    oracle = subprocess.run([str(a.oracle.resolve()), str(a.rounds)], capture_output=True, timeout=120)
    assert oracle.returncode == 0 and not oracle.stderr, oracle
    (a.output / 'oracle.stdout').write_bytes(oracle.stdout)
    for cpus in (1, 2, 4):
        for name, runner in [('baseline', a.baseline), ('candidate', a.runner)]:
            cmd = [str(runner.resolve()), '--memory', '--network', 'off', '--cpus', str(cpus),
                   '--accelerator', 'pure', '--metrics', '--max-instructions', '2000000000',
                   str(a.fixture.resolve()), str(a.rounds)]
            r = subprocess.run(cmd, capture_output=True, timeout=180)
            label = f'{name}-{cpus}'
            (a.output / (label + '.stdout')).write_bytes(r.stdout)
            (a.output / (label + '.stderr')).write_bytes(r.stderr)
            assert (r.returncode, r.stdout) == (0, oracle.stdout), (label, r.returncode, r.stdout, r.stderr[-6000:])
            values = dict(line.split(': ', 1) for line in r.stderr.decode().splitlines() if line.startswith('trames '))
            native = json.loads(values['trames native metrics'])
            runtime = json.loads(values['trames metrics'])
            assert native['faults'] == native['rejected'] == native['fallbacks'] == 0, native
            assert native['function_calls'][2] == 2*a.rounds, native
            assert native['bytes_written'] == 2*a.rounds*4096, native
            assert runtime['service_requests'] == 0, runtime
            if name == 'candidate' and cpus > 1:
                assert runtime['worker_futex_waits'] > 0 and runtime['worker_task_resumes'] > 0, runtime
            report['rows'].append(dict(variant=name, cpus=cpus, runtime=runtime, native=native))
            print(label, 'PASS', f"{runtime['seconds']:.6f}s", 'epochs', runtime['parallel_rounds'],
                  'waits', runtime.get('worker_futex_waits', 0), 'resumes', runtime.get('worker_task_resumes', 0), flush=True)
finally:
    (a.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
