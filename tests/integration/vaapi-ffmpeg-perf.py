#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Measure completed FFmpeg hardware frames; compare with native VideoToolbox.

Build tests/fixtures/vaapi-ffmpeg-bench.c on ARM64 Linux and macOS against the
same FFmpeg release. Use a continuous H.264 file, not -stream_loop, and an
isolated guest store containing the Linux runtime dependencies.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shlex
import statistics
import subprocess
import time
import uuid


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('runner', 'storage', 'program', 'driver', 'sample', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--native-program', type=Path)
    parser.add_argument('--rootfs', type=Path)
    parser.add_argument('--libraries', type=Path, help='Flat directory of exact Linux .so dependencies')
    parser.add_argument('--frames', type=int, required=True)
    parser.add_argument('--cpus', type=int, nargs='+', default=[4])
    parser.add_argument('--modes', nargs='+', choices=('sync', 'export', 'download'),
                        default=['sync', 'export', 'download'])
    parser.add_argument('--repeats', type=int, default=3)
    args = parser.parse_args()
    if args.frames <= 0 or args.repeats <= 0 or min(args.cpus) <= 0:
        parser.error('frames, repeats and cpus must be positive')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    files = {'bench': args.program, 'trames_drv_video.so': args.driver, 'sample.h264': args.sample}
    if args.libraries:
        files.update({p.name: p for p in args.libraries.iterdir() if p.is_file()})
    hashes = {name: sha256(path) for name, path in sorted(files.items())}
    identity = hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest()[:16]
    guest = '/opt/trames-ffmpeg-perf-' + identity + '-' + uuid.uuid4().hex[:8]
    base = [str(args.runner.resolve()), '--storage', str(args.storage.resolve()),
            '--no-windows', '--no-pty', '--video', '--network', 'off',
            '--memory-mib', 'auto', '--timeout', '180']
    setup = base.copy()
    if args.rootfs:
        setup += ['--rootfs', str(args.rootfs.resolve())]
    for name, path in files.items():
        setup += ['--exec-file', guest + '/' + name + '=' + str(path.resolve())]
    # Import is outside timed runs; content-addressed paths bind every artifact.
    imported = subprocess.run(setup + ['/bin/true'], capture_output=True)
    (out / 'import.stderr').write_bytes(imported.stderr)
    imported.check_returncode()
    env = {**os.environ, 'MVK_CONFIG_LOG_LEVEL': '1'}
    records = []
    report = {'complete': False, 'sha256': hashes, 'runner_sha256': sha256(args.runner),
              'frames': args.frames, 'repeats': args.repeats, 'records': records}
    if args.native_program:
        report['native_program_sha256'] = sha256(args.native_program)
    # Sequential runs: never compete for the hardware decoder or CPU.
    variants = [('guest', cpus, mode) for cpus in args.cpus for mode in args.modes]
    if args.native_program:
        variants += [('native', 0, mode) for mode in args.modes if mode != 'export']
    for platform, cpus, mode in variants:
        for repeat in range(args.repeats):
            if platform == 'native':
                command = [str(args.native_program.resolve()), str(args.sample.resolve()), mode, str(args.frames)]
            else:
                script = 'export ' + ' '.join(k + '=' + shlex.quote(v) for k, v in {
                    'LIBVA_DRIVER_NAME': 'trames', 'LIBVA_DRIVERS_PATH': guest,
                    'LD_LIBRARY_PATH': guest}.items())
                script += '; exec ' + shlex.join([guest + '/bench', guest + '/sample.h264', mode, str(args.frames)])
                command = base + ['--cpus', str(cpus), '/bin/sh', '-ec', script]
            before = resource.getrusage(resource.RUSAGE_CHILDREN)
            began = time.monotonic()
            result = subprocess.run(command, capture_output=True, env=env, timeout=210)
            elapsed = time.monotonic() - began
            after = resource.getrusage(resource.RUSAGE_CHILDREN)
            label = f'{platform}-{cpus}-{mode}-{repeat + 1}'
            (out / (label + '.stdout')).write_bytes(result.stdout)
            (out / (label + '.stderr')).write_bytes(result.stderr)
            result.check_returncode()
            measured = json.loads(result.stdout)
            if measured['frames'] != args.frames or measured['seconds'] <= 0:
                raise RuntimeError('invalid completed frame count or interval')
            record = {'platform': platform, 'cpus': cpus, 'mode': mode, 'repeat': repeat + 1,
                      'process_wall': elapsed, 'host_user': after.ru_utime - before.ru_utime,
                      'host_system': after.ru_stime - before.ru_stime, **measured}
            records.append(record)
            (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
            print(json.dumps(record), flush=True)
    report['medians'] = []
    for platform, cpus, mode in variants:
        rows = [r for r in records if (r['platform'], r['cpus'], r['mode']) == (platform, cpus, mode)]
        report['medians'].append({'platform': platform, 'cpus': cpus, 'mode': mode, **{
            key: statistics.median(r[key] for r in rows) for key in (
                'fps', 'seconds', 'consume_seconds', 'process_wall', 'host_user', 'host_system')}})
    report['complete'] = True
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
