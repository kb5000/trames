#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixed native memory work overlapping metadata/pipe IO; alternating same ELF."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import resource
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline', type=Path, required=True)
p.add_argument('--candidate', type=Path, required=True)
p.add_argument('--host-oracle', type=Path, required=True)
p.add_argument('--fixture', type=Path, default=Path('tests/fixtures/bin/epoch-services'))
p.add_argument('--output', type=Path, required=True)
p.add_argument('--iterations', type=int, default=512)
p.add_argument('--samples', type=int, default=2)
p.add_argument('--modes', nargs='+', choices=['metadata', 'pipe', 'file', 'vector', 'unix', 'mapped-read', 'mapped-shared'], default=['metadata', 'pipe'])
p.add_argument('--reuse-baseline', type=Path, help='reuse exact-binary/input baseline rows from a previous batch')
a = p.parse_args()
assert a.iterations > 0 and a.samples > 0
a.output.mkdir(parents=True, exist_ok=False)
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
report = dict(host=platform.machine(), guest='aarch64', cpus=4, guest_threads=2,
              iterations=a.iterations, native_bytes_per_fill=1 << 20,
              candidate_sha256=digest(a.candidate), baseline_sha256=digest(a.baseline),
              fixture_sha256=digest(a.fixture), oracle_sha256=digest(a.host_oracle), rows=[],
              limits='Synthetic workload, same static original musl plus exact NativeCall fill stub. '
              'Host C oracle checks deterministic output; not a guest CPU oracle, app benchmark, '
              'or iPad prediction. Native bytes/iteration and syscall counts are fixed, no elapsed-time loops.')
if a.reuse_baseline:
    old = json.loads(a.reuse_baseline.read_text())
    for key in ('baseline_sha256', 'fixture_sha256', 'iterations', 'cpus'):
        assert old[key] == report[key], ('baseline input changed', key)
    report['rows'] = [x for x in old['rows'] if x['version'] == 'before']
    report['rows'] = [x for x in report['rows'] if x['mode'] in a.modes]
    assert {x['mode'] for x in report['rows']} == set(a.modes)
    report['baseline_reused_sha256'] = digest(a.reuse_baseline)
    report['baseline_reuse_limit'] = 'Previously measured baseline; not a newly interleaved host-load control.'
try:
    for mode in a.modes:
        oracle = subprocess.run([str(a.host_oracle.resolve()), mode, str(a.iterations)], capture_output=True, timeout=120)
        assert oracle.returncode == 0 and not oracle.stderr, oracle
        (a.output / (mode + '-oracle.log')).write_bytes(oracle.stdout)
        for sample in range(a.samples):
            modes = [('before', a.baseline), ('after', a.candidate)]
            if a.reuse_baseline: modes = [('after', a.candidate)]
            if sample % 2: modes.reverse()
            for name, binary in modes:
                cmd = [str(binary.resolve()), '--memory', '--network', 'off', '--cpus', '4',
                       '--accelerator', 'pure', '--metrics', '--max-instructions', '2000000000',
                       str(a.fixture.resolve()), mode, str(a.iterations)]
                cpu_before = resource.getrusage(resource.RUSAGE_CHILDREN)
                began = time.monotonic()
                r = subprocess.run(cmd, capture_output=True, timeout=180)
                elapsed = time.monotonic() - began
                cpu_after = resource.getrusage(resource.RUSAGE_CHILDREN)
                label = f'{mode}-{sample}-{name}'
                (a.output / (label + '.stdout')).write_bytes(r.stdout)
                (a.output / (label + '.stderr')).write_bytes(r.stderr)
                assert (r.returncode, r.stdout) == (0, oracle.stdout), (label, r.returncode, r.stdout, r.stderr[-3000:])
                values = dict(line.split(': ', 1) for line in r.stderr.decode().splitlines() if line.startswith('trames '))
                native = json.loads(values['trames native metrics'])
                runtime = json.loads(values['trames metrics'])
                assert native['faults'] == native['rejected'] == 0 and native['function_calls'][2] > 0, native
                assert native['function_calls'][2] == a.iterations and native['bytes_written'] == a.iterations * (1 << 20), native
                assert native['fallbacks'] == 0, native
                assert native['worker_calls'] > 0, native
                if name == 'after': assert runtime['serviced_syscalls'] > 0, runtime
                row = dict(mode=mode, sample=sample, version=name, wall_seconds=elapsed,
                           host_cpu_seconds=cpu_after.ru_utime + cpu_after.ru_stime - cpu_before.ru_utime - cpu_before.ru_stime,
                           native=native, runtime=runtime)
                report['rows'].append(row)
                print(mode, sample, name, round(elapsed, 3), 'rounds', runtime['parallel_rounds'],
                      'services', runtime.get('serviced_syscalls', 0), flush=True)
finally:
    (a.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
