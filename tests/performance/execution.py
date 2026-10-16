#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Manual alternating interpreter comparison; unchanged guest programs, no CI timing gates.

The store must contain signed Alpine Vim/Python/GCC and upstream CoreMark at
/root/coremark/coremark.exe. Build the existing cpu-memory and smp fixtures first.
Use --emulated-host only for ARM64 runner code-generation checks under QEMU;
those timings are not physical ARM64 performance measurements.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import resource
import shutil
import statistics
import subprocess
import tempfile
import time

import nonjit

ROOT = Path(__file__).resolve().parents[2]
WORKLOADS = ('coremark', 'cpu-memory', 'smp-work', 'vim-default', 'python-imports', 'gcc-build-run')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def metrics(stderr):
    records = {}
    for line in stderr.decode().splitlines():
        label, text = line.split(': ', 1)
        records[label] = json.loads(text)
    runtime = records.pop('trames metrics')
    for profile in records.values():
        assert not profile.get('faults', 0) and not profile.get('rejected', 0), profile
    return {'runtime': runtime, 'other': records}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--baseline-commit', required=True)
    parser.add_argument('--store', type=Path, required=True)
    parser.add_argument('--samples', type=int, default=3)
    parser.add_argument('--coremark-iterations', type=int, default=8000)
    parser.add_argument('--cpu-memory-fixture', type=Path,
                        help='Optional longer build of cpu-memory.S; checksum and instruction count are still checked')
    parser.add_argument('--emulated-host', action='store_true')
    parser.add_argument('--workloads', nargs='+', choices=WORKLOADS, default=WORKLOADS)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert args.samples > 0 and args.coremark_iterations > 0
    binaries = {'baseline': args.baseline.resolve(), 'candidate': args.candidate.resolve()}
    libc = ROOT / 'target/musl-hybrid/libc.so'
    fixtures = {name: ROOT / 'tests/fixtures/bin' / name for name in ('cpu-memory', 'smp')}
    if args.cpu_memory_fixture:
        fixtures['cpu-memory'] = args.cpu_memory_fixture.resolve()
    qemu = shutil.which(os.getenv('QEMU_AARCH64', 'qemu-aarch64'))
    expected_smp = None
    if 'smp-work' in args.workloads:
        result = subprocess.run([qemu, '-cpu', 'max', str(fixtures['smp']), 'work', '4', '3000000'],
                                capture_output=True, timeout=120, check=True)
        assert not result.stderr, result.stderr
        expected_smp = result.stdout
    report = dict(baseline_commit=args.baseline_commit,
                  host=platform.platform(), host_arch=platform.machine(),
                  cpu_quota=Path('/sys/fs/cgroup/cpu.max').read_text().strip(),
                  cpu_affinity=len(os.sched_getaffinity(0)),
                  cpu_affinity_ids=sorted(os.sched_getaffinity(0)), emulated_host=args.emulated_host,
                  runner_sha256={name: digest(path) for name, path in binaries.items()},
                  libc_sha256=digest(libc), store_manifest_sha256=digest(args.store / 'manifest'),
                  fixture_sha256={name: digest(path) for name, path in fixtures.items()},
                  coremark_iterations=args.coremark_iterations,
                  scope='Alternating baseline/candidate, identical guest programs and fresh cloned app stores; '
                        'warm caches on a shared host; affinity recorded, frequency not controlled. CoreMark default single-thread -O2. '
                        'Apps use the same complete hybrid musl, pure routing, allocator routing off. '
                        'CoreMark and static CPU/SMP fixtures use their original libc. '
                        'QEMU host timings, when selected, test ARM64 code generation, not device speed.',
                  rows=[])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='trames-execution-') as directory:
        tmp = Path(directory)
        source = tmp / 'program.c'
        source.write_text(nonjit.C_SOURCE)
        for name in args.workloads:
            results = {'baseline': [], 'candidate': []}
            workload = dict(workload=name, samples=results)
            report['rows'].append(workload)
            for sample in range(args.samples):
                order = list(binaries.items())
                if sample % 2:
                    order.reverse()
                for mode, binary in order:
                    prefix = [qemu, '-cpu', 'max'] if args.emulated_host else []
                    command = [*prefix, str(binary), '--network', 'off', '--cpus',
                               '4' if name == 'smp-work' else '1', '--metrics',
                               '--max-instructions', '100000000000', '--env', 'HOME=/root',
                               '--env', 'TERM=xterm-256color', '--env', 'PYTHONHASHSEED=0']
                    app = name in ('vim-default', 'python-imports', 'gcc-build-run')
                    store = None
                    if app or name == 'coremark':
                        store = tmp / 'store'
                        shutil.copytree(args.store, store)
                        command += ['--storage', str(store)]
                    else:
                        command += ['--memory']
                    if app:
                        command += ['--musl-libc', str(libc), '--accelerator', 'pure']
                    if name == 'coremark':
                        command += ['--guest', '/root/coremark/coremark.exe', '0', '0', '0x66',
                                    str(args.coremark_iterations), '7', '1', '2000']
                    elif name == 'cpu-memory':
                        command += [str(fixtures[name])]
                        expected = b'cpu-memory-ok\n'
                    elif name == 'smp-work':
                        command += [str(fixtures['smp']), 'work', '4', '3000000']
                        expected = expected_smp
                    elif name == 'vim-default':
                        command += ['--pty', '--guest', '/usr/bin/vim', '/tmp/performance-empty.txt']
                    elif name == 'python-imports':
                        command += ['--guest', '/usr/bin/python3', '-c', nonjit.PYTHON]
                        expected = b'python: 42\n'
                    else:
                        command += ['--file', '/tmp/performance.c=' + str(source), '--guest',
                                    '/bin/sh', '-c', 'gcc -O2 /tmp/performance.c -o /tmp/performance-app && /tmp/performance-app']
                        expected = b'gcc: 333283335000\n'
                    cpu_before = resource.getrusage(resource.RUSAGE_CHILDREN)
                    began = time.monotonic()
                    if name == 'vim-default':
                        seconds, profile = nonjit.vim(command, metrics)
                        row = {'seconds': seconds, 'endpoint': 'spawn to normal first visible cursor', **profile}
                    else:
                        result = subprocess.run(command, capture_output=True, timeout=600)
                        elapsed = time.monotonic() - began
                        assert result.returncode == 0, (name, mode, result.stdout[-3000:], result.stderr[-3000:])
                        profile = metrics(result.stderr)
                        if name == 'coremark':
                            stdout = result.stdout.decode()
                            assert 'Correct operation validated.' in stdout, stdout
                            seconds = float(re.search(r'Total time \(secs\):\s*([0-9.]+)', stdout)[1])
                            assert seconds >= 10, 'Increase CoreMark iterations for >= 10 second scoring'
                            row = {'seconds': seconds, 'endpoint': 'CoreMark timed kernels',
                                   'score': float(re.search(r'Iterations/Sec\s*:\s*([0-9.]+)', stdout)[1]),
                                   'stdout': stdout, **profile}
                        else:
                            assert result.stdout == expected, (name, result.stdout, expected)
                            seconds = elapsed if app else profile['runtime']['seconds']
                            row = {'seconds': seconds, 'endpoint': 'spawn through exit/final sync' if app else 'guest execution',
                                   'stdout': result.stdout.decode(), **profile}
                    cpu_after = resource.getrusage(resource.RUSAGE_CHILDREN)
                    row.update(host_wall_seconds=time.monotonic() - began, sample=sample + 1,
                               host_cpu_seconds_including_exit=cpu_after.ru_utime + cpu_after.ru_stime
                               - cpu_before.ru_utime - cpu_before.ru_stime)
                    results[mode].append(row)
                    args.output.write_text(json.dumps(report, indent=2) + '\n')
                    print(name, sample + 1, mode, round(seconds, 6), flush=True)
                    if store:
                        shutil.rmtree(store)
            # CoreMark's elapsed-time formatting executes slightly different
            # instruction counts; only the fixed assembly fixture is invariant.
            if name == 'cpu-memory':
                assert len({row['runtime']['instructions'] for runs in results.values() for row in runs}) == 1
            medians = {mode: statistics.median(row['seconds'] for row in runs) for mode, runs in results.items()}
            workload.update(median_seconds=medians, speedup=medians['baseline'] / medians['candidate'])
            args.output.write_text(json.dumps(report, indent=2) + '\n')
    print([(row['workload'], round(row['speedup'], 3)) for row in report['rows']], flush=True)


if __name__ == '__main__':
    main()
