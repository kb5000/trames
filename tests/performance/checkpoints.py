#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""One alternating batch for sparse commit and CPU dispatch, with app controls."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import resource
import shutil
import statistics
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('allocator_bench', ROOT / 'tests/performance/allocator.py')
allocator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(allocator)
digest = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--baseline-commit', required=True)
    parser.add_argument('--vim-store', type=Path, required=True)
    parser.add_argument('--python-store', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    current = ROOT / 'target/release/trames-run'
    libc = ROOT / 'target/musl-hybrid/libc.so'
    archive = ROOT / 'target/alpine-download/alpine-minirootfs-3.22.6-aarch64.tar.gz'
    fixtures = {name: ROOT / 'tests/fixtures/bin' / name
                for name in ('musl-allocator', 'native-parallel', 'region-memory')}
    oracle = {}
    for name, arguments in [('native-parallel', ['bench', '512']), ('region-memory', ['400000'])]:
        result = subprocess.run([os.getenv('QEMU_AARCH64', 'qemu-aarch64'), '-cpu', 'max',
            '-L', str(ROOT / 'target/alpine-build/rootfs'), str(fixtures[name]), *arguments],
            capture_output=True, timeout=120)
        assert result.returncode == 0 and not result.stderr, result
        oracle[name] = result.stdout
    workloads = [('allocator-original', count) for count in (1, 4)] + [
        ('allocator-native', 4), ('native-copy', 4), ('same-page-counters', 1),
        ('same-page-counters', 4), ('vim-default', 1), ('python-imports', 1)]
    rows = []
    with tempfile.TemporaryDirectory(prefix='trames-checkpoint-bench-') as directory:
        for name, count in workloads:
            samples = {'baseline': [], 'current': []}
            for sample in range(3):
                choices = [('baseline', args.baseline), ('current', current)]
                if sample % 2:
                    choices.reverse()
                for mode, binary in choices:
                    command = [str(binary), '--network', 'off', '--cpus', str(count),
                        '--musl-libc', str(libc), '--accelerator', 'pure', '--metrics',
                        '--max-instructions', '100000000000', '--env', 'HOME=/root',
                        '--env', 'TERM=xterm-256color']
                    if name == 'allocator-native':
                        command += ['--native-allocator']
                    store = None
                    if name in ('vim-default', 'python-imports'):
                        store = Path(directory) / 'store'
                        shutil.copytree(args.vim_store if name == 'vim-default' else args.python_store, store)
                        command += ['--storage', str(store)]
                        command += ['--pty', '--guest', '/usr/bin/vim', '/tmp/performance-empty.txt'] \
                            if name == 'vim-default' else ['--guest', '/usr/bin/python3', '-c', allocator.nonjit.PYTHON]
                        expected = b'python: 42\n'
                    else:
                        fixture, arguments = ('musl-allocator', ['bench', '96']) if name.startswith('allocator') \
                            else ('native-parallel', ['bench', '512']) if name == 'native-copy' \
                            else ('region-memory', ['400000'])
                        command += ['--memory', '--rootfs', str(archive), '--exec-file',
                            '/bin/bench=' + str(fixtures[fixture]), '--guest', '/bin/bench', *arguments]
                        expected = b'musl-allocator-ok\n' if fixture == 'musl-allocator' else oracle[fixture]
                    before = resource.getrusage(resource.RUSAGE_CHILDREN)
                    began = time.monotonic()
                    if name == 'vim-default':
                        seconds, profile = allocator.nonjit.vim(command, allocator.metrics)
                    else:
                        result = subprocess.run(command, capture_output=True, timeout=300)
                        assert (result.returncode, result.stdout) == (0, expected), (name, mode, result)
                        profile = allocator.metrics(result.stderr)
                        seconds = profile['runtime']['seconds']
                    wall = time.monotonic() - began
                    after = resource.getrusage(resource.RUSAGE_CHILDREN)
                    assert bool(profile['native']['allocator_calls']) == (name == 'allocator-native'), profile
                    samples[mode].append(dict(seconds=seconds, host_wall_seconds=wall,
                        host_cpu_seconds=after.ru_utime + after.ru_stime - before.ru_utime - before.ru_stime,
                        **profile))
                    print(name, count, sample, mode, round(seconds, 6), flush=True)
                    if store:
                        shutil.rmtree(store)
            medians = {mode: statistics.median(x['seconds'] for x in values) for mode, values in samples.items()}
            rows.append(dict(workload=name, cpus=count, samples=samples,
                median_seconds=medians, speedup=medians['baseline'] / medians['current']))
    report = dict(baseline_commit=args.baseline_commit, baseline_sha256=digest(args.baseline),
        current_sha256=digest(current), libc_sha256=digest(libc), rootfs_sha256=digest(archive),
        fixture_sha256={name: digest(path) for name, path in fixtures.items()},
        fixture_source_sha256={name: digest(ROOT / 'tests/fixtures' / (name + '.c')) for name in fixtures},
        store_manifest_sha256={'vim': digest(args.vim_store / 'manifest'),
                               'python': digest(args.python_store / 'manifest')},
        source_sha256={str(path.relative_to(ROOT)): digest(path) for path in sorted((ROOT / 'crates').rglob('*.rs'))},
        host=platform.platform(), host_arch=platform.machine(), cpu_affinity=len(os.sched_getaffinity(0)),
        cpu_quota=Path('/sys/fs/cgroup/cpu.max').read_text().strip(), rows=rows,
        limits='Three alternating samples on a shared x86_64 development host; identical guest ELF, full musl and cloned app stores. Allocator routing is off except allocator-native. Runtime seconds exclude import; Vim seconds measure spawn to first normal cursor visibility. Worker CPU/body sums overlap host wall/checkpoint waits. Native copy: four threads, 512 copies of 1 MiB each, 2 GiB total. Counters: four disjoint counters in one page, 400000 increments each. No timing assertions or iPad performance claim.')
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print([(r['workload'], r['cpus'], round(r['speedup'], 3)) for r in rows], flush=True)


if __name__ == '__main__':
    main()
