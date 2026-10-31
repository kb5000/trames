#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare unchanged private I/O and opt-in host mounts with native macOS.

Use an isolated directory. A separate native Linux VM run can be supplied with
--linux-results; stop that VM before these macOS/Trames measurements. Initial
file population and fsync are outside each fixed-work fixture interval.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import statistics
import subprocess
import time
import uuid

CASES = {'read64k': 32768, 'write64k': 16384, 'read4k': 100000, 'stat': 100000}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('runner', 'baseline', 'program', 'native', 'rootfs', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--linux-results', type=Path)
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--interop', action='store_true', help='compare native notifications and held flock as well')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    shared = out / 'shared'
    (shared / 'deep').mkdir(parents=True, exist_ok=True)
    (out / 'native').mkdir(exist_ok=True)
    program = '/opt/hostfs-bench-' + digest(args.program)[:12] + '-' + uuid.uuid4().hex[:8]
    base = ['--no-windows', '--no-pty', '--network', 'off', '--cpus', '4', '--timeout', '180']
    stores = {'before': out / 'before-store', 'after': out / 'after-store'}
    runners = {'before': args.baseline.resolve(), 'after': args.runner.resolve()}
    for label in stores:
        result = subprocess.run([str(runners[label]), '--storage', str(stores[label]), *base,
                                 '--rootfs', str(args.rootfs.resolve()), '--exec-file',
                                 program + '=' + str(args.program.resolve()), '/bin/sh', '-ec',
                                 'mkdir -p /perf/normal/deep'], capture_output=True)
        (out / (label + '-setup.stderr')).write_bytes(result.stderr)
        result.check_returncode()
    variants = ['before-private', 'after-private', 'after-private-idle-mount', 'after-host', 'native-mac']
    if args.interop:
        variants = ['before-private', 'after-private', 'before-host', 'after-host', 'after-host-watch',
                    'after-host-flock', 'native-mac', 'native-mac-watch', 'native-mac-flock']
    jobs = [(variant, case, repeat + 1) for repeat in range(args.repeats)
            for variant in variants for case in CASES]
    random.Random(20261031).shuffle(jobs)
    records = []
    report = {'complete': False, 'scope': 'single process, cached 8 MiB file, fixed work; no rendering/network',
              'cases': CASES, 'repeats': args.repeats, 'sha256': {
                  key: digest(getattr(args, key)) for key in ('runner', 'baseline', 'program', 'native', 'rootfs')},
              'records': records}
    if args.linux_results:
        report['linux_native'] = json.loads(args.linux_results.read_text())
    for variant, case, repeat in jobs:
        if variant.startswith('native-mac'):
            command = [str(args.native.resolve()), str(out / 'native/file'), case, str(CASES[case])]
        else:
            label = 'before' if variant.startswith('before-') else 'after'
            command = [str(runners[label]), '--storage', str(stores[label]), *base]
            host = '-host' in variant
            if host or variant == 'after-private-idle-mount':
                command += ['--mount', '/mnt/shared=' + str(shared)]
            path = '/mnt/shared/deep/file' if host else '/perf/normal/deep/file'
            command += [program, path, case, str(CASES[case])]
        if variant.endswith('-watch') or variant.endswith('-flock'):
            command += [variant.rsplit('-', 1)[1]]
        began = time.monotonic()
        result = subprocess.run(command, capture_output=True, timeout=210,
                                env={**os.environ, 'MVK_CONFIG_LOG_LEVEL': '1'})
        wall = time.monotonic() - began
        name = f'{variant}-{case}-{repeat}'
        (out / (name + '.stdout')).write_bytes(result.stdout)
        (out / (name + '.stderr')).write_bytes(result.stderr)
        result.check_returncode()
        measured = json.loads(result.stdout)
        if measured['operations'] != CASES[case] or measured['seconds'] <= 0:
            raise RuntimeError('invalid completed work')
        record = {'variant': variant, 'case': case, 'repeat': repeat, 'process_wall': wall, **measured}
        records.append(record)
        (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(record), flush=True)
    report['medians'] = []
    for variant in variants:
        for case in CASES:
            rows = [r for r in records if r['variant'] == variant and r['case'] == case]
            report['medians'].append({'variant': variant, 'case': case, **{
                key: statistics.median(r[key] for r in rows) for key in (
                    'seconds', 'mib_per_second', 'ops_per_second', 'sync_seconds', 'process_wall')},
                'seconds_range': [min(r['seconds'] for r in rows), max(r['seconds'] for r in rows)]})
    report['complete'] = True
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
