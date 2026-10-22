#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real ARM64 musl Unix socket exchanges and owner-only SCM_RIGHTS fallback."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner', type=Path, required=True)
p.add_argument('--oracle', type=Path, required=True)
p.add_argument('--fixture', type=Path, default=Path('tests/fixtures/bin/worker-unix'))
p.add_argument('--output', type=Path, required=True)
p.add_argument('--cpus', type=int, nargs='+', choices=(1, 2, 4), default=(1, 2, 4))
p.add_argument('--modes', nargs='+', choices=('read', 'vector', 'socket', 'message', 'epoll', 'waitall', 'rights'),
               default=('read', 'vector', 'socket', 'message', 'epoll', 'waitall', 'rights'))
p.add_argument('--affinities', nargs='+', choices=('pinned', 'spread'), default=('pinned', 'spread'))
p.add_argument('--baseline', type=Path, help='also collect previous-runner structural counters on the same work')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
report = dict(scope='Two real static musl guest threads exchange fixed tokens and execute native fills. '
                   'Host C assertions/output oracle. Not a CPU differential oracle or timing benchmark.',
              runner_sha256=sha(a.runner), fixture_sha256=sha(a.fixture), oracle_sha256=sha(a.oracle),
              baseline_sha256=sha(a.baseline) if a.baseline else None, passed=False, rows=[])
try:
    for mode in a.modes:
        rounds = 16 if mode == 'rights' else 128
        for affinity in a.affinities:
            tail = [mode, affinity, str(rounds)]
            oracle = subprocess.run([str(a.oracle.resolve()), *tail], capture_output=True, timeout=30)
            assert oracle.returncode == 0 and not oracle.stderr, oracle
            (a.output / f'{mode}-{affinity}-oracle.stdout').write_bytes(oracle.stdout)
            for cpus in a.cpus:
                versions = [('after', a.runner)]
                if a.baseline:
                    versions.insert(0, ('before', a.baseline))
                for version, binary in versions:
                    label = f'{mode}-{affinity}-{cpus}-{version}'
                    command = [str(binary.resolve()), '--memory', '--network', 'off', '--cpus', str(cpus),
                               '--accelerator', 'pure', '--metrics', '--max-instructions', '200000000',
                               str(a.fixture.resolve()), *tail]
                    r = subprocess.run(command, capture_output=True, timeout=60)
                    (a.output / (label+'.stdout')).write_bytes(r.stdout)
                    (a.output / (label+'.stderr')).write_bytes(r.stderr)
                    assert (r.returncode, r.stdout) == (0, oracle.stdout), (label, r.returncode, r.stdout, r.stderr[-6000:])
                    values = dict(line.split(': ', 1) for line in r.stderr.decode().splitlines() if line.startswith('trames '))
                    native = json.loads(values['trames native metrics'])
                    runtime = json.loads(values['trames metrics'])
                    assert native['function_calls'][2] == rounds * 2 and native['bytes_written'] == rounds * 8192, native
                    assert native['faults'] == native['rejected'] == native['fallbacks'] == 0, native
                    if cpus > 1 and version == 'after':
                        assert runtime['worker_socket_syscalls'] > 0 and native['worker_calls'] > 0, runtime
                        if affinity == 'pinned':
                            assert runtime['peak_parallel_cpus'] == 1, runtime
                        if mode != 'rights':
                            assert runtime['serviced_syscalls'] == runtime['service_requests'] == 0, runtime
                    report['rows'].append(dict(mode=mode, affinity=affinity, cpus=cpus, version=version,
                                               rounds=rounds, runtime=runtime, native=native))
                    print(label, 'PASS', 'epochs', runtime['parallel_rounds'],
                          'services', runtime['serviced_syscalls'],
                          'local sockets', runtime.get('worker_socket_syscalls', 0), flush=True)
    report['passed'] = True
finally:
    (a.output / 'results.json').write_text(json.dumps(report, indent=2)+'\n')
