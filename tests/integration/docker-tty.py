#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise original Docker -it with default runc and overlay2 in a guest.

Accept an installed store containing default-probe (or --image). Copy it so the
input disk remains unchanged. Host stdin feeds the guest terminal only after the
container asks for input. No host Docker, runtime/security overrides or network
access is required by the payload.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--store', type=Path, required=True)
parser.add_argument('--runner', type=Path, default=Path('target/release/trames-run'))
parser.add_argument('--image', default='default-probe')
parser.add_argument('--output', type=Path, default=Path('target/docker-tty'))
parser.add_argument('--timeout', type=float, default=1200)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=False)
script = r'''
set -eux
mount -t tmpfs -o mode=755 tmpfs /run
mkdir -p /dev/shm
mount -t tmpfs -o size=64m,mode=1777 tmpfs /dev/shm
daemon=
cleanup() {
  test -z "$daemon" || { kill "$daemon" 2>/dev/null || :; wait "$daemon" || :; }
  cat /tmp/dockerd.log
  dmesg
}
trap cleanup EXIT
dockerd >/tmp/dockerd.log 2>&1 &
daemon=$!
for i in $(seq 1 180); do
  grep -q 'API listen on' /tmp/dockerd.log && break
  kill -0 "$daemon"
  sleep 1
done
grep 'API listen on' /tmp/dockerd.log
test "$(docker info --format '{{.DefaultRuntime}} {{.Driver}}')" = 'runc overlay2'
echo DOCKER_TTY_DAEMON_PASS
docker run -it --rm --name tty-probe "$TTY_IMAGE" /bin/sh -ec '
  test -t 0; test -t 1; test -t 2
  test "$(stat -c %i /dev/pts/ptmx)" = 2
  echo DOCKER_TTY_INPUT_READY
  read -r reply
  test "$reply" = trames-tty-roundtrip
  echo DOCKER_TTY_ROUNDTRIP_PASS
  echo DOCKER_TTY_STDERR_PASS >&2'
test -z "$(docker ps -aq --filter name=tty-probe)"
echo DOCKER_TTY_CLEANUP_PASS
'''
(args.output / 'guest.sh').write_text(script)
report = {'runner_sha256': hashlib.sha256(args.runner.read_bytes()).hexdigest(),
          'store_manifest_sha256': hashlib.sha256((args.store / 'manifest').read_bytes()).hexdigest(),
          'image': args.image, 'cpus': 4}
with tempfile.TemporaryDirectory(prefix='trames-docker-tty-') as tmp:
    store = Path(tmp) / 'store'
    shutil.copytree(args.store, store)
    command = [str(args.runner.resolve()), '--storage', str(store), '--network', 'full',
               '--cpus', '4', '--pty', '--max-instructions', '50000000000',
               '--env', 'TTY_IMAGE=' + args.image, '--guest', '/bin/sh', '-ec', script]
    began = time.monotonic()
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
    timer = threading.Timer(args.timeout, process.kill)
    timer.start()
    sent = False
    try:
        with (args.output / 'runtime.log').open('wb') as log:
            for line in process.stdout:
                log.write(line)
                log.flush()
                normalized = line.strip()
                if normalized == b'DOCKER_TTY_INPUT_READY' and not sent:
                    process.stdin.write(b'trames-tty-roundtrip\n')
                    process.stdin.flush()
                    sent = True
                if normalized.startswith(b'DOCKER_TTY_'):
                    print(normalized.decode(), flush=True)
        code = process.wait()
    finally:
        timer.cancel()
        if process.poll() is None:
            process.kill()
            process.wait()
        process.stdin.close()
text = (args.output / 'runtime.log').read_text()
lines = {line.strip() for line in text.splitlines()}
report.update(exit_code=code, seconds=round(time.monotonic() - began, 3), input_sent=sent,
              checks={name: 'DOCKER_TTY_' + name + '_PASS' in lines
                      for name in ('DAEMON', 'ROUNDTRIP', 'STDERR', 'CLEANUP')})
(args.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2), flush=True)
assert code == 0 and sent and all(report['checks'].values()), report
