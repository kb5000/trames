#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Occasional same-guest before/after batch; never a physical-iPad claim."""
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
RUNNER = os.getenv('TRAMES_RUNNER', str(ROOT / 'target/release/trames-run'))
BASELINE = os.environ['TRAMES_BASELINE_RUNNER']
LIBC = ROOT / 'target/musl-hybrid/libc.so'
FIXTURE = ROOT / 'tests/fixtures/bin/accelerator-full-musl'
ARCHIVE = ROOT / 'target/alpine-download/alpine-minirootfs-3.22.6-aarch64.tar.gz'
digest = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
rows = []
with tempfile.TemporaryDirectory(prefix='trames-bulk-bench-') as directory:
    data = Path(directory) / 'sort-input'
    data.write_text(''.join(f'{i:05d} ' + 'payload' * 8 + '\n' for i in range(2048, 0, -1)))
    expected = b''.join(sorted(data.read_bytes().splitlines(keepends=True)))
    fixture_args = ['--exec-file', '/bin/bench=' + str(FIXTURE), '--guest', '/bin/bench']
    workloads = [(f'copy-{n}', [*fixture_args, str(n), 'copy'], b'accelerator-bench-ok\n')
                 for n in (32, 65536, 1048576)]
    workloads += [('shared-fill-65536', [*fixture_args, '65536', 'shared-fill'], b'accelerator-bench-ok\n'),
                  ('alpine-sort-2048', ['--file', '/root/sort-input=' + str(data), '--guest', '/bin/busybox', 'sort', '-n', '/root/sort-input'], expected)]
    for name, args, wanted in workloads:
        modes = {'previous': [], 'current': []}
        for sample in range(3):
            order = [('previous', BASELINE), ('current', RUNNER)]
            if sample % 2:
                order.reverse()
            for key, runner in order:
                result = subprocess.run([runner, '--memory', '--cpus', '1', '--rootfs', str(ARCHIVE), '--musl-libc', str(LIBC), '--metrics', '--max-instructions', '2000000000', '--accelerator', 'pure', *args], capture_output=True, timeout=240)
                assert (result.returncode, result.stdout) == (0, wanted), (name, key, result.returncode, result.stderr[-4000:])
                native, runtime = [json.loads(line.split(': ', 1)[1]) for line in result.stderr.decode().splitlines()]
                modes[key].append({'native': native, 'runtime': runtime})
        medians = {key: statistics.median(s['runtime']['seconds'] for s in samples) for key, samples in modes.items()}
        rows.append({'workload': name, 'modes': modes, 'median_seconds': medians, 'speedup': medians['previous'] / medians['current']})
        print(name, medians, 'speedup', round(rows[-1]['speedup'], 3), flush=True)
report = {'host': platform.platform(), 'host_architecture': platform.machine(), 'description': 'Three alternating before/after samples per workload, one guest CPU, identical new musl and guest binaries on both hosts. x86_64 development executor interprets ARM64 guests directly; NEON speed is not measured here. Runtime time includes guest startup, excludes host rootfs import. No physical-iPad or universal application speed claim.', 'baseline_binary_sha256': digest(BASELINE), 'current_binary_sha256': digest(RUNNER), 'libc_sha256': digest(LIBC), 'fixture_sha256': digest(FIXTURE), 'source_sha256': {p: digest(ROOT / p) for p in ['crates/trames-memory/src/native_access.rs', 'crates/trames-memory/src/backing.rs', 'crates/trames-vfs/src/mapped.rs', 'crates/trames-native/src/lib.rs', 'crates/trames-accel/src/native.rs', 'crates/trames-accel/src/strings.rs', 'accelerator/musl/strings.c', 'tests/fixtures/accelerator.c', 'tests/performance/bulk-native.py']}, 'rows': rows}
(ROOT / 'target/musl-hybrid/bulk-benchmark.json').write_text(json.dumps(report, indent=2) + '\n')
