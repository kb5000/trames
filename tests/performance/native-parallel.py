#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""One alternating fixed-work native pthread batch and unchanged BusyBox sort."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import resource
import statistics
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', type=Path, required=True)
parser.add_argument('--baseline-commit', required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--iterations', type=int, default=128)
args = parser.parse_args()
current = ROOT / 'target/release/trames-run'
fixture = ROOT / 'tests/fixtures/bin/native-parallel'
libc = ROOT / 'target/musl-hybrid/libc.so'
archive = ROOT / 'target/alpine-download/alpine-minirootfs-3.22.6-aarch64.tar.gz'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
oracle = subprocess.run([os.getenv('QEMU_AARCH64', 'qemu-aarch64'), '-cpu', 'max',
    '-L', str(ROOT / 'target/alpine-build/rootfs'), str(fixture), 'bench', str(args.iterations)],
    capture_output=True, timeout=60)
assert oracle.returncode == 0 and not oracle.stderr, oracle
rows = []
with tempfile.TemporaryDirectory(prefix='trames-native-parallel-bench-') as folder:
    data = Path(folder) / 'sort-input'
    data.write_text(''.join(f'{i:05d} ' + 'payload' * 8 + '\n' for i in range(2048, 0, -1)))
    sort_expected = b''.join(sorted(data.read_bytes().splitlines(keepends=True)))
    for workload in ('four-thread-native-copy', 'alpine-sort-2048'):
        cpus = (1, 2, 4) if workload == 'four-thread-native-copy' else (1,)
        modes = [(f'{name}-{count}', binary, count) for count in cpus
            for name, binary in [('baseline', args.baseline), ('current', current)]]
        samples = {name: [] for name, _, _ in modes}
        guest = ['--exec-file', '/bin/bench=' + str(fixture), '--guest', '/bin/bench', 'bench', str(args.iterations)] \
            if workload == 'four-thread-native-copy' else \
            ['--file', '/root/sort-input=' + str(data), '--guest', '/bin/busybox', 'sort', '-n', '/root/sort-input']
        expected = oracle.stdout if workload == 'four-thread-native-copy' else sort_expected
        for sample in range(3):
            for name, binary, count in modes if sample % 2 == 0 else list(reversed(modes)):
                cmd = [str(binary), '--memory', '--network', 'off', '--cpus', str(count), '--rootfs', str(archive),
                    '--musl-libc', str(libc), '--accelerator', 'pure', '--metrics', '--max-instructions', '2000000000', *guest]
                before = resource.getrusage(resource.RUSAGE_CHILDREN)
                start = time.monotonic()
                result = subprocess.run(cmd, capture_output=True, timeout=240)
                wall = time.monotonic() - start
                after = resource.getrusage(resource.RUSAGE_CHILDREN)
                assert (result.returncode, result.stdout) == (0, expected), (name, result.returncode, result.stderr[-4000:])
                native, runtime = [json.loads(line.split(': ', 1)[1]) for line in result.stderr.decode().splitlines()]
                assert native['faults'] == native['rejected'] == 0, native
                cpu = after.ru_utime + after.ru_stime - before.ru_utime - before.ru_stime
                samples[name].append(dict(native=native, runtime=runtime, host_wall_seconds=wall,
                    host_cpu_seconds=cpu, host_average_cpus=cpu / wall))
                print(workload, sample, name, f'{runtime["seconds"]:.6f}s',
                    f'rounds={runtime["parallel_rounds"]}', f'overlap={runtime.get("peak_parallel_native_calls", 0)}', flush=True)
        medians = {name: statistics.median(value['runtime']['seconds'] for value in values) for name, values in samples.items()}
        rows.append(dict(workload=workload, samples=samples, median_seconds=medians,
            speedup_over_same_cpu_baseline={str(count): medians[f'baseline-{count}'] / medians[f'current-{count}'] for count in cpus},
            current_scaling={str(count): medians['current-1'] / medians[f'current-{count}'] for count in cpus}))
report = dict(baseline_commit=args.baseline_commit, baseline_binary_sha256=digest(args.baseline),
    current_binary_sha256=digest(current), fixture_sha256=digest(fixture), libc_sha256=digest(libc),
    fixture_source_sha256=digest(ROOT / 'tests/fixtures/native-parallel.c'), host=platform.platform(),
    host_architecture=platform.machine(), cpu_quota=Path('/sys/fs/cgroup/cpu.max').read_text().strip(),
    cpu_affinity=len(os.sched_getaffinity(0)), guest_threads=4, bytes_per_copy=1048576,
    copies_per_thread=args.iterations, rows=rows,
    source_sha256={str(path.relative_to(ROOT)): digest(path) for path in sorted((ROOT / 'crates').rglob('*.rs'))},
    limits='Three alternating samples, fixed work and identical full musl/guest ELF on both runtimes. Runtime seconds exclude rootfs import; host wall/CPU include startup/import. cpu_seconds is summed worker elapsed, includes lock waits, and is not host CPU occupancy. x86_64 development interpreter, no physical iPad or NEON timing claim.')
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps([{key: row[key] for key in ('workload', 'median_seconds', 'speedup_over_same_cpu_baseline', 'current_scaling')} for row in rows], indent=2))
