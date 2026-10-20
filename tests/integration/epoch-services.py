#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Original static musl with a fill stub, FD/pipe services and real mapping edits."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner', type=Path, required=True)
p.add_argument('--fixture', type=Path, required=True)
p.add_argument('--oracle', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--modes', nargs='+', choices=['metadata', 'pipe', 'file', 'vector'], default=['metadata', 'pipe', 'file', 'vector'])
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
rows = []
try:
    for mode in a.modes:
        expected = subprocess.run([str(a.oracle.resolve()), mode, '256'], capture_output=True, timeout=60)
        assert expected.returncode == 0 and not expected.stderr, expected
        for cpus in (1, 2, 4):
            r = subprocess.run([str(a.runner.resolve()), '--memory', '--network', 'off',
                '--cpus', str(cpus), '--accelerator', 'pure', '--metrics',
                '--max-instructions', '2000000000', str(a.fixture.resolve()), mode, '256'],
                capture_output=True, timeout=120)
            label = f'{mode}-{cpus}'
            (a.output / (label + '.stdout')).write_bytes(r.stdout)
            (a.output / (label + '.stderr')).write_bytes(r.stderr)
            assert (r.returncode, r.stdout) == (0, expected.stdout), (label, r.returncode, r.stdout, r.stderr[-4000:])
            values = dict(line.split(': ', 1) for line in r.stderr.decode().splitlines() if line.startswith('trames '))
            native = json.loads(values['trames native metrics'])
            runtime = json.loads(values['trames metrics'])
            assert native['faults'] == native['rejected'] == native['fallbacks'] == 0, native
            assert native['function_calls'][2] == 256 and native['bytes_written'] == 256 << 20, native
            if cpus == 1:
                assert runtime['service_requests'] == runtime['serviced_syscalls'] == 0, runtime
            else:
                assert runtime['parallel_rounds'] > 0, runtime
            # Short native calls may finish before a peer on a busy host. The
            # deterministic worker test proves native progress with a held RPC.
            rows.append(dict(mode=mode, cpus=cpus, native=native, runtime=runtime))
            print('PASS', label, 'services', runtime['serviced_syscalls'], flush=True)
finally:
    (a.output / 'results.json').write_text(json.dumps(dict(
        fixture_sha256=hashlib.sha256(a.fixture.read_bytes()).hexdigest(), rows=rows,
        oracle='Native C semantic output oracle, not a guest CPU differential oracle; no timing claims.'), indent=2) + '\n')
