#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Manual, interleaved comparison of real guest programs on identical stores.

Preserve the baseline release binary before rebuilding. Stores must already
contain signed Alpine Vim and the M6 developer packages. No package downloads,
Vim configuration changes, or timing assertions occur in this benchmark.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import selectors
import shutil
import statistics
import subprocess
import tempfile
import time

PYTHON = """import asyncio, hashlib, json, pathlib, sqlite3, ssl, threading
data = json.loads('{"value":42}')
db = sqlite3.connect(':memory:')
db.execute('create table values_test(value integer)')
db.execute('insert into values_test values (?)', (data['value'],))
assert db.execute('select value from values_test').fetchone() == (42,)
assert hashlib.sha256(b'abc').hexdigest() == 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad'
print('python: 42')
"""
C_SOURCE = """#include <stdio.h>
int main(void) {
    unsigned long long sum = 0;
    for (unsigned i = 0; i < 10000; ++i) sum += (unsigned long long)i * i;
    printf("gcc: %llu\\n", sum);
    return sum != 333283335000ULL;
}
"""


def sha256(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def metrics(stderr):
    lines = stderr.decode().splitlines()
    records = [json.loads(line.removeprefix('trames metrics: ')) for line in lines
               if line.startswith('trames metrics: ')]
    assert len(records) == 1 and len(lines) == 1, stderr[-4000:]
    return records[0]


def vim(command, read_metrics=metrics):
    # First cursor visibility follows Vim's normal startup/config/plugin work.
    # The terminal responses prevent probing delays from dominating CPU time.
    start = time.monotonic()
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    output = bytearray()
    pending = bytearray()
    ready = None
    queries = [(b'\x1b[6n', b'\x1b[1;1R'), (b'\x1b[>c', b'\x1b[>0;100;0c'),
               (b'\x1b[c', b'\x1b[?1;2c')]
    try:
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ)
            while process.poll() is None and time.monotonic() - start < 120:
                for key, _ in selector.select(.01):
                    data = key.fileobj.read1(65536)
                    output.extend(data)
                    pending.extend(data)
                    for query, reply in queries:
                        while query in pending:
                            position = pending.index(query)
                            del pending[position:position + len(query)]
                            process.stdin.write(reply)
                            process.stdin.flush()
                    # Keep enough tail to handle a query split across read calls.
                    pending[:] = pending[-16:]
                    if ready is None and b'\x1b[?25h' in output:
                        ready = time.monotonic() - start
                        process.stdin.write(b':qa!\r')
                        process.stdin.flush()
            assert process.poll() is not None, 'Vim startup/exit timed out'
        remaining, stderr = process.communicate(timeout=10)
        output.extend(remaining)
        assert process.returncode == 0 and ready is not None and b'E1286' not in output, (
            process.returncode, stderr[-4000:], output[-4000:])
        return ready, read_metrics(stderr)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--baseline-backend', choices=('reference', 'cached', 'cpp'),
                        default='cached', help='Backend used by the preserved baseline binary')
    parser.add_argument('--vim-store', type=Path, required=True)
    parser.add_argument('--developer-store', type=Path, required=True)
    parser.add_argument('--samples', type=int, default=3)
    parser.add_argument('--workloads', nargs='+', choices=('vim-default', 'python-imports', 'gcc-build-run'),
                        default=['vim-default', 'python-imports', 'gcc-build-run'])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert args.samples >= 1
    configurations = [
        ('baseline-' + args.baseline_backend, args.baseline.resolve(), args.baseline_backend),
        ('candidate-cached', args.candidate.resolve(), None),
    ]
    results = []
    with tempfile.TemporaryDirectory(prefix='trames-nonjit-') as directory:
        temporary = Path(directory)
        source = temporary / 'performance.c'
        source.write_text(C_SOURCE)
        for workload in args.workloads:
            for sample in range(args.samples):
                # Alternate order; every run starts from the identical fixture,
                # including viminfo, pycache, package files and inode metadata.
                ordered = configurations if sample % 2 == 0 else configurations[::-1]
                for label, runner, backend in ordered:
                    store = temporary / 'store'
                    original = args.vim_store if workload == 'vim-default' else args.developer_store
                    shutil.copytree(original, store)
                    command = [str(runner), '--storage', str(store), '--network', 'off',
                               '--metrics', '--max-instructions', '100000000000',
                               '--env', 'HOME=/root', '--env', 'TERM=xterm-256color']
                    if backend is not None:
                        command += ['--backend', backend]
                    if workload == 'vim-default':
                        command += ['--pty', '--guest', '/usr/bin/vim', '/tmp/performance-empty.txt']
                        seconds, profile = vim(command)
                        endpoint = 'process spawn to first visible cursor'
                    else:
                        if workload == 'python-imports':
                            command += ['--guest', '/usr/bin/python3', '-c', PYTHON]
                            expected = b'python: 42\n'
                        else:
                            command += ['--file', '/tmp/performance.c=' + str(source), '--guest',
                                        '/bin/sh', '-c', 'gcc -O2 /tmp/performance.c -o /tmp/performance-app && /tmp/performance-app']
                            expected = b'gcc: 333283335000\n'
                        start = time.monotonic()
                        process = subprocess.run(command, capture_output=True, timeout=600)
                        seconds = time.monotonic() - start
                        assert process.returncode == 0 and process.stdout == expected, (
                            command[-4:], process.returncode, process.stdout[-4000:], process.stderr[-4000:])
                        profile = metrics(process.stderr)
                        endpoint = 'process spawn to successful exit (including final sync)'
                    result = dict(workload=workload, sample=sample + 1, configuration=label,
                                  seconds=seconds, endpoint=endpoint, metrics=profile)
                    results.append(result)
                    print(json.dumps(result), flush=True)
                    shutil.rmtree(store)
    summaries = []
    for workload in args.workloads:
        baseline = statistics.median(r['seconds'] for r in results
                                     if r['workload'] == workload and r['configuration'] == configurations[0][0])
        for label, _, _ in configurations:
            values = [r['seconds'] for r in results if r['workload'] == workload and r['configuration'] == label]
            median = statistics.median(values)
            summaries.append(dict(workload=workload, configuration=label, median_seconds=median,
                                  min_seconds=min(values), max_seconds=max(values),
                                  speedup=baseline / median))
    report = dict(scope='Linux host; real unmodified guest programs; warm host caches; no physical iPad claim',
                  host=dict(platform=platform.platform(), machine=platform.machine(), logical_cpus=os.cpu_count()),
                  inputs=dict(baseline_sha256=sha256(args.baseline), candidate_sha256=sha256(args.candidate),
                              baseline_backend=args.baseline_backend,
                              vim_manifest_sha256=sha256(args.vim_store / 'manifest'),
                              developer_manifest_sha256=sha256(args.developer_store / 'manifest'),
                              python_source_sha256=hashlib.sha256(PYTHON.encode()).hexdigest(),
                              c_source_sha256=hashlib.sha256(C_SOURCE.encode()).hexdigest()),
                  samples=args.samples, results=results, summaries=summaries)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(summaries), flush=True)


if __name__ == '__main__':
    main()
