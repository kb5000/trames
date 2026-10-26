#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run an already built Release core-test binary; do not build while timing."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--iterations', type=int, default=20_000)
    parser.add_argument('--samples', type=int, default=5)
    args = parser.parse_args()
    if not 1 <= args.iterations <= 1_000_000 or not 1 <= args.samples <= 20:
        parser.error('iterations must be 1..1000000; samples must be 1..20')
    binary = args.binary.resolve(strict=True)
    root = Path(__file__).resolve().parents[2]
    args.output.mkdir(parents=True, exist_ok=False)
    report = {
        'schema': 1,
        'host_arch': platform.machine(),
        'host_os': platform.system(),
        'host_available_cpus': len(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else os.cpu_count(),
        'linux_cpu_quota': Path('/sys/fs/cgroup/cpu.max').read_text().strip()
        if Path('/sys/fs/cgroup/cpu.max').exists() else None,
        'iterations_per_worker': args.iterations,
        'samples': args.samples,
        'cpu_accounting': 'sum of parent and caller thread CPU clocks; sampled on each live thread',
        'binary_sha256': digest(binary),
        'source_sha256': {
            name: digest(root / name) for name in (
                'runtime/crates/trames-session/src/coordination_cost.rs',
                'runtime/crates/trames-session/src/workers.rs',
            )
        },
        'scope': (
            'Fixed u64 increment and checked result per operation, one outstanding '
            'request per caller. A: shared object with Mutex / atomic diagnostic; '
            'B: the SAME workers exchange queued messages or single-outstanding '
            'mailboxes and service incoming requests while awaiting their own '
            'reply; ZERO extra service/coordinator threads. Independent objects '
            'are assigned to the next worker in a ring; the shared object belongs '
            'to worker 0. One worker is the degenerate local-delivery control. '
            'C: production '
            'Request/Reply/Event/Pool transport with its retained View. '
            'C excludes interpreter, Linux/VFS, global stop/join/commit/dispatch: '
            'a transport lower bound, not full coordinator cost. '
            'No thread/channel/mapping initialization in the measured interval; '
            'caller completion and thread join are included. '
            'Parallel ns/operation is inverse aggregate throughput, not individual '
            'call latency. This is not guest performance or an iPad prediction.'
        ),
        'rows': [],
    }
    env = dict(os.environ, TRAMES_COORD_ITERATIONS=str(args.iterations),
               TRAMES_COORD_SAMPLES=str(args.samples))
    cmd = [str(binary), 'workers::coordination_cost::coordination_transport_profile',
           '--exact', '--ignored', '--nocapture', '--test-threads=1']
    report['command'] = cmd
    began = time.monotonic()
    try:
        with (args.output / 'run.log').open('w') as log:
            proc = subprocess.Popen(cmd, env=env, cwd=root, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, bufsize=1)
            try:
                for line in proc.stdout:
                    log.write(line)
                    # libtest may prepend "test <name> ... " to the first
                    # println; keep the header without rerunning measurements.
                    if 'COORDINATOR_COST_HEADER ' in line:
                        report['header'] = json.loads(line.split('COORDINATOR_COST_HEADER ', 1)[1])
                    elif line.startswith('COORDINATOR_COST '):
                        row = json.loads(line.split(' ', 1)[1])
                        report['rows'].append(row)
                        print(row['workers'], 'shared' if row['shared_resource'] else 'independent',
                              row['sample'], row['mode'], round(row['ns_per_operation'], 1),
                              'ns/op', flush=True)
                report['exit_code'] = proc.wait()
            finally:
                if proc.poll() is None:
                    proc.terminate()
                    proc.wait(timeout=15)
        assert report['exit_code'] == 0, report['exit_code']
        assert report['header']['iterations_per_worker'] == args.iterations
        assert len(report['rows']) == 3 * 2 * 5 * args.samples, 'missing samples'
        report['summary'] = []
        for workers in (1, 2, 4):
            for shared in (False, True):
                for mode in ('shared_mutex', 'shared_atomic', 'worker_queue',
                             'worker_mailbox_busy_poll',
                             'coordinator_rpc_lower_bound'):
                    rows = [r for r in report['rows'] if r['workers'] == workers
                            and r['shared_resource'] == shared and r['mode'] == mode]
                    assert len(rows) == args.samples
                    values = [r['ns_per_operation'] for r in rows]
                    cpu_ns = [r['cpu_seconds'] * 1e9 / r['operations'] for r in rows]
                    report['summary'].append(dict(
                        workers=workers, shared_resource=shared, mode=mode,
                        ns_per_operation_median=statistics.median(values),
                        host_cpu_ns_per_operation_median=statistics.median(cpu_ns),
                        ns_per_operation_min=min(values), ns_per_operation_max=max(values)))
    finally:
        report['wall_seconds_including_setup'] = time.monotonic() - began
        report['raw_log_sha256'] = digest(args.output / 'run.log')
        (args.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
