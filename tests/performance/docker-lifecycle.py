#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Profile default guest Docker create/exec once, without repeating acceptance.

Input store must contain original dockerd/containerd/runc and an imported Alpine
image. A private store copy is used. This never invokes host Docker. Optional
PC sampling uses pc-sampler.c and an optimized release runner with debug symbols.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import time


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--store', type=Path, required=True)
    parser.add_argument('--image', default='default-probe')
    parser.add_argument('--cpus', type=int, default=4)
    parser.add_argument('--sampler', type=Path)
    parser.add_argument('--timeout', type=float, default=2400)
    parser.add_argument('--max-instructions', type=int, default=50_000_000_000)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert args.cpus > 0 and args.timeout > 0 and args.max_instructions > 0
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    runner = args.runner.resolve()
    script = Path(__file__).with_suffix('.sh')
    env = os.environ.copy()
    if args.sampler:
        env.update(LD_PRELOAD=str(args.sampler.resolve()),
                   TRAMES_PC_SAMPLES=str(out / 'host.pcs'),
                   TRAMES_PC_EXECUTABLE=str(runner))
    report = dict(scope=__doc__, runner_sha256=sha(runner),
                  store_manifest_sha256=sha(args.store / 'manifest'),
                  shell_sha256=sha(script), cpus=args.cpus, image=args.image,
                  pc_sampling=bool(args.sampler), markers=[])
    # Same input directory is copied; the original installed disk is never run.
    with tempfile.TemporaryDirectory(prefix='trames-docker-profile-') as tmp:
        store = Path(tmp) / 'store'
        shutil.copytree(args.store, store)
        command = [str(runner), '--storage', str(store), '--network', 'full',
                   '--cpus', str(args.cpus), '--metrics', '--max-instructions',
                   str(args.max_instructions), '--env', 'PROFILE_IMAGE=' + args.image,
                   '--guest', '/bin/sh', '-ec', script.read_text()]
        expired = threading.Event()
        began = time.monotonic()
        with (out / 'runtime.log').open('wb') as log:
            process = subprocess.Popen(command, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, env=env)

            def timeout():
                expired.set()
                process.kill()

            timer = threading.Timer(args.timeout, timeout)
            timer.daemon = True
            timer.start()
            try:
                for line in process.stdout:
                    log.write(line)
                    log.flush()
                    if line.startswith(b'TRAMES_DOCKER_PROFILE_'):
                        marker = line.decode().strip().removeprefix('TRAMES_DOCKER_PROFILE_')
                        report['markers'].append(dict(name=marker, seconds=time.monotonic()-began))
                        print(marker, flush=True)
                code = process.wait()
            finally:
                timer.cancel()
                if process.poll() is None:
                    process.kill()
                    process.wait()
        report.update(exit_code=code, timed_out=expired.is_set(), seconds=time.monotonic()-began)
    text = (out / 'runtime.log').read_text()
    report['metrics'] = next((json.loads(line.split(': ', 1)[1]) for line in text.splitlines()
                              if line.startswith('trames metrics: ')), None)
    markers = {row['name']: row['seconds'] for row in report['markers']}
    report['stages'] = {stage: markers[stage+'_END']-markers[stage+'_BEGIN']
                        for stage in ('DAEMON', 'CREATE', 'EXEC')
                        if stage+'_BEGIN' in markers and stage+'_END' in markers}
    report['completed'] = (code == 0 and not expired.is_set()
                           and 'EXEC_END' in markers and 'CLEANUP_END' in markers)
    (out / 'results.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2), flush=True)
    if not report['completed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
