#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Static musl NativeCall -> actual Vulkan driver with concurrent native work.

Fixed GPU fills and exact byte assertions; structural counts, no timing claim.
Select the host driver with VK_DRIVER_FILES and optional TRAMES_VULKAN_LIBRARY.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner', type=Path, required=True)
p.add_argument('--baseline', type=Path)
p.add_argument('--fixture', type=Path, default=Path('tests/fixtures/bin/worker-gpu'))
p.add_argument('--output', type=Path, required=True)
p.add_argument('--cpus', type=int, nargs='+', choices=(1, 2, 4), default=(1, 2, 4))
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
checksum = 0
for i in range(1024):
    checksum = (checksum * 33 + (i * 31 + 7) % 256) & 0xffffffff
gpu_checksum = 0
for i in range(32):
    gpu_checksum = (gpu_checksum * 33 + 0x12345600 + i) & 0xffffffff
report = dict(scope=__doc__, runner_sha256=sha(a.runner), fixture_sha256=sha(a.fixture),
              baseline_sha256=sha(a.baseline) if a.baseline else None,
              passed=False, rows=[])
environment = dict(os.environ, MESA_SHADER_CACHE_DISABLE='true')
try:
    for mode in ('query', 'fill'):
        for affinity in ('pinned', 'spread'):
            expected = f'worker-gpu {mode} {affinity} 32 {checksum:08x} {gpu_checksum if mode == "fill" else 0:08x}\n'.encode()
            for cpus in a.cpus:
                versions = [('after', a.runner)]
                if a.baseline:
                    versions.insert(0, ('before', a.baseline))
                for version, binary in versions:
                    label = f'{mode}-{affinity}-{cpus}-{version}'
                    cmd = [str(binary.resolve()), '--memory', '--network', 'off',
                           '--cpus', str(cpus), '--accelerator', 'pure', '--vulkan',
                           '--metrics', '--max-instructions', '1000000000',
                           str(a.fixture.resolve()), mode, affinity]
                    r = subprocess.run(cmd, env=environment, capture_output=True, timeout=60)
                    (a.output / (label + '.stdout')).write_bytes(r.stdout)
                    (a.output / (label + '.stderr')).write_bytes(r.stderr)
                    assert (r.returncode, r.stdout) == (0, expected), (label, r.returncode, r.stdout, r.stderr[-4000:])
                    values = dict(line.split(': ', 1) for line in r.stderr.decode().splitlines() if line.startswith('trames '))
                    runtime = json.loads(values['trames metrics'])
                    native = json.loads(values['trames native metrics'])
                    assert runtime['runtime_error'] is None
                    assert native['function_calls'][2] == 1024 and native['bytes_written'] == 1 << 30
                    assert native['faults'] == native['rejected'] == native['fallbacks'] == 0
                    if mode == 'fill':
                        assert runtime['gpu_download_bytes'] > 0
                        assert runtime['gpu_direct_bytes'] == runtime['gpu_upload_bytes'] + runtime['gpu_download_bytes']
                        assert runtime['gpu_buffered_bytes'] == 0
                    if version == 'after' and cpus > 1:
                        assert runtime['worker_gpu_calls'] > 0
                        assert runtime['service_requests'] == runtime['serviced_syscalls'] == 0
                        if affinity == 'pinned':
                            assert runtime['peak_parallel_cpus'] == 1
                    report['rows'].append(dict(mode=mode, affinity=affinity, cpus=cpus,
                                               version=version, runtime=runtime, native=native))
                    print(label, 'PASS', 'epochs', runtime['parallel_rounds'],
                          'worker_gpu', runtime.get('worker_gpu_calls', 0), flush=True)
    report['passed'] = True
finally:
    (a.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
