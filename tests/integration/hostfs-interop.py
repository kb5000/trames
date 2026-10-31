#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Use separate native/guest processes to check shared-directory wakeups."""
import argparse
import fcntl
import json
from pathlib import Path
import shutil
import subprocess
import time


def wait_file(path, process, timeout=30):
    deadline = time.monotonic() + timeout
    while not path.exists():
        if process.poll() is not None or time.monotonic() >= deadline:
            raise RuntimeError(f'guest stopped or timed out waiting for {path}')
        time.sleep(0.002)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('runner', 'rootfs', 'tools', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    shared = out / 'shared'
    (shared / 'watch').mkdir(parents=True)
    (shared / 'lock').write_bytes(b'lock')
    shutil.copyfile(Path(__file__).resolve().parents[1] / 'fixtures/hostfs-interop.py', shared / 'test.py')
    base = [str(args.runner.resolve()), '--storage', str(out / 'store'), '--no-windows', '--no-pty',
            '--network', 'off', '--timeout', '30', '--mount', '/work=' + str(shared),
            '--mount-ro', '/usr=' + str((args.tools / 'usr').resolve()),
            '--mount-ro', '/lib=' + str((args.tools / 'lib').resolve())]
    records = []
    cases = [(mode, cpus, busy) for mode in ('notify-read', 'notify-poll', 'notify-epoll', 'notify-close-reuse', 'flock-wait', 'flock-hold')
             for cpus, busy in ((1, False), (4, True))]
    for index, (mode, cpus, busy) in enumerate(cases):
        for name in ('ready', 'closed', 'result', 'watch/native-created'):
            (shared / name).unlink(missing_ok=True)
        with (shared / 'lock').open('r+b') as lock:
            if mode == 'flock-wait':
                fcntl.flock(lock, fcntl.LOCK_EX)
            command = base + ['--cpus', str(cpus)]
            if index == 0:
                command += ['--rootfs', str(args.rootfs.resolve())]
            command += ['/usr/bin/python3', '/work/test.py', mode] + (['busy'] if busy else [])
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                wait_file(shared / 'ready', process)
                if mode == 'notify-close-reuse':
                    wait_file(shared / 'closed', process)
                # Let the guest reach its blocking call after publishing ready.
                time.sleep(0.03)
                began = time.monotonic()
                if mode.startswith('notify-'):
                    (shared / 'watch/native-created').write_bytes(b'native producer')
                elif mode == 'flock-wait':
                    fcntl.flock(lock, fcntl.LOCK_UN)
                else:
                    try:
                        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    except BlockingIOError:
                        pass
                    else:
                        raise AssertionError('guest dup did not retain native flock')
                wait_file(shared / 'result', process, 5)
                wake_ms = (time.monotonic() - began) * 1000
                stdout, stderr = process.communicate(timeout=30)
                assert process.returncode == 0 and b'INTEROP_OK' in stdout, (stdout, stderr)
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                record = dict(case=mode, cpus=cpus, concurrent_guest=busy, wake_to_result_ms=wake_ms,
                              result=json.loads((shared / 'result').read_text()),
                              stdout=stdout.decode(), stderr=stderr.decode())
                records.append(record)
                print(json.dumps(record), flush=True)
                (out / 'result.json').write_text(json.dumps(dict(complete=False, records=records), indent=2) + '\n')
            finally:
                if process.poll() is None:
                    process.kill()
                stdout,stderr=process.communicate()
                (out / f'{index}-{mode}.stdout').write_bytes(stdout)
                (out / f'{index}-{mode}.stderr').write_bytes(stderr)
    (out / 'result.json').write_text(json.dumps(dict(complete=True, records=records), indent=2) + '\n')


if __name__ == '__main__':
    main()
