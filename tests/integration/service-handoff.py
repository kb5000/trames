#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Six file readers plus native work, including a single eligible host slot."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner', type=Path, required=True)
p.add_argument('--oracle', type=Path, required=True)
p.add_argument('--fixture', type=Path, default=Path('tests/fixtures/bin/service-handoff'))
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
report = dict(scope='Static ARM64 musl: six guest readers x four 64 KiB reads and '
                   '32 native 1 MiB fills; independent host C assertions/output oracle. '
                   'Pinned mode restricts every guest task to CPU 0 with 2/4 CPUs configured. '
                   'Functional checks, not timing or a new guest CPU differential oracle.',
              runner_sha256=sha(a.runner), oracle_sha256=sha(a.oracle),
              fixture_sha256=sha(a.fixture), passed=False, rows=[])
try:
    for mode in ('pread', 'preadv', 'read'):
        for affinity in ('pinned', 'spread'):
            expected = subprocess.run([str(a.oracle.resolve()), mode, affinity], capture_output=True, timeout=30)
            assert expected.returncode == 0 and not expected.stderr, expected
            (a.output / f'{mode}-{affinity}-oracle.stdout').write_bytes(expected.stdout)
            for cpus in (2, 4):
                label = f'{mode}-{affinity}-{cpus}'
                cmd = [str(a.runner.resolve()), '--storage', str((a.output / (label+'-store')).resolve()),
                       '--network', 'off', '--cpus', str(cpus), '--accelerator', 'pure', '--metrics',
                       '--max-instructions', '2000000000', str(a.fixture.resolve()), mode, affinity]
                r = subprocess.run(cmd, capture_output=True, timeout=90)
                (a.output / (label+'.stdout')).write_bytes(r.stdout)
                (a.output / (label+'.stderr')).write_bytes(r.stderr)
                assert (r.returncode, r.stdout) == (0, expected.stdout), (label, r.returncode, r.stdout, r.stderr[-6000:])
                values = dict(line.split(': ', 1) for line in r.stderr.decode().splitlines() if line.startswith('trames '))
                runtime = json.loads(values['trames metrics'])
                native = json.loads(values['trames native metrics'])
                assert runtime['worker_service_waits'] > 0 and runtime['serviced_syscalls'] > 0, runtime
                assert 1 <= runtime['peak_disk_reads'] <= cpus and runtime['disk_reads'] > 0, runtime
                assert runtime['disk_read_bytes'] == runtime['disk_reads'] * 65536, runtime
                if affinity == 'pinned':
                    assert runtime['peak_parallel_cpus'] == 1, runtime
                assert native['function_calls'][2] == 32 and native['worker_calls'] == 32, native
                assert native['bytes_written'] == 32 << 20, native
                assert native['faults'] == native['rejected'] == native['fallbacks'] == 0, native
                report['rows'].append(dict(mode=mode, affinity=affinity, cpus=cpus, runtime=runtime, native=native))
                print(label, 'PASS', 'services', runtime['serviced_syscalls'], 'peak disk', runtime['peak_disk_reads'], flush=True)
    report['passed'] = True
finally:
    (a.output / 'results.json').write_text(json.dumps(report, indent=2)+'\n')
