#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Established loopback TCP, native C oracle, identical ELF before/after."""
import argparse
import hashlib
import json
from pathlib import Path
import socket
import subprocess
import threading
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner', type=Path, required=True)
p.add_argument('--baseline', type=Path)
p.add_argument('--oracle', type=Path, required=True)
p.add_argument('--fixture', type=Path, default=Path('tests/fixtures/bin/worker-tcp'))
p.add_argument('--output', type=Path, required=True)
p.add_argument('--blocking', action='store_true', help='exercise native readiness, backpressure and fragmented replies')
p.add_argument('--cpus', type=int, nargs='+', choices=(1, 2, 4), help='limit affected CPU configurations on follow-up checks')
p.add_argument('--reuse-oracles', type=Path, help='reuse verified native reference output from an identical fixture/oracle run')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
report = dict(scope=__doc__, runner_sha256=sha(a.runner), baseline_sha256=sha(a.baseline) if a.baseline else None,
              fixture_sha256=sha(a.fixture), oracle_sha256=sha(a.oracle), passed=False, rows=[])
if a.reuse_oracles:
    previous = json.loads((a.reuse_oracles / 'results.json').read_text())
    assert previous['passed'] and all(previous[k] == report[k] for k in ('fixture_sha256', 'oracle_sha256'))
    report['oracles_reused_from'] = str(a.reuse_oracles)
expected = b''.join(bytes([97 + i % 26]) * 1024 for i in range(128))

def exchange(command, family):
    errors = []
    with socket.socket(socket.AF_INET if family == '4' else socket.AF_INET6) as listener:
        listener.bind(('127.0.0.1' if family == '4' else '::1', 0))
        listener.listen(1)
        listener.settimeout(15)
        port = listener.getsockname()[1]
        def serve():
            try:
                with listener.accept()[0] as peer:
                    peer.settimeout(15)
                    if a.blocking:
                        time.sleep(.05)
                    data = bytearray()
                    while chunk := peer.recv(16384):
                        data.extend(chunk)
                        assert len(data) <= len(expected)
                    assert data == expected
                    if a.blocking:
                        time.sleep(.05)
                        peer.sendall(b'P')
                        time.sleep(.03)
                        peer.sendall(b'ASS\n')
                    else:
                        peer.sendall(b'PASS\n')
            except Exception as error:
                errors.append(repr(error))
        thread = threading.Thread(target=serve, daemon=True)
        thread.start()
        result = subprocess.run([*command, str(port)], capture_output=True, timeout=60)
        thread.join(20)
        assert not thread.is_alive() and not errors, (errors, result.returncode, result.stderr[-3000:])
        return result

try:
    for family in ('4', '6'):
        for mode in (('blocking-write', 'blocking-vector') if a.blocking else ('write', 'vector')):
            oracle_path = f'oracle-{family}-{mode}.stdout'
            if a.reuse_oracles:
                expected_stdout = (a.reuse_oracles / oracle_path).read_bytes()
            else:
                oracle = exchange([str(a.oracle.resolve()), family, mode], family)
                assert oracle.returncode == 0 and not oracle.stderr, oracle
                expected_stdout = oracle.stdout
            (a.output / oracle_path).write_bytes(expected_stdout)
            for cpus in (a.cpus or ((1, 2, 4) if a.blocking or family == '4' and mode == 'write' else (2, 4))):
                versions = [('after', a.runner)]
                if a.baseline:
                    versions.insert(0, ('before', a.baseline))
                for version, runner in versions:
                    label = f'{family}-{mode}-{cpus}-{version}'
                    command = [str(runner.resolve()), '--memory', '--network', 'loopback', '--cpus', str(cpus),
                               '--accelerator', 'pure', '--metrics', '--max-instructions', '2000000000',
                               str(a.fixture.resolve()), family, mode]
                    result = exchange(command, family)
                    (a.output / f'{label}.stdout').write_bytes(result.stdout)
                    (a.output / f'{label}.stderr').write_bytes(result.stderr)
                    assert (result.returncode, result.stdout) == (0, expected_stdout), (label, result.returncode, result.stdout, result.stderr[-3000:])
                    values = dict(line.split(': ', 1) for line in result.stderr.decode().splitlines() if line.startswith('trames '))
                    runtime = json.loads(values['trames metrics'])
                    native = json.loads(values['trames native metrics'])
                    assert runtime['runtime_error'] is None
                    assert native['faults'] == native['fallbacks'] == native['rejected'] == 0
                    if version == 'after' and cpus > 1:
                        assert runtime['worker_socket_syscalls'] >= 100, runtime
                        assert native['worker_calls'] > 0, native
                        if a.blocking:
                            assert runtime['worker_socket_syscalls'] >= 131, runtime
                            assert runtime['worker_io_waits'] >= 3 and runtime['worker_task_resumes'] >= 3, runtime
                    report['rows'].append(dict(label=label, runtime=runtime, native=native))
                    print(label, 'PASS rounds', runtime['parallel_rounds'], 'local TCP', runtime['worker_socket_syscalls'], flush=True)
    report['passed'] = True
finally:
    (a.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
