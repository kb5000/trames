#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Original dockerd startup with 4GiB guest budgets, 8 CPUs and host VA sampling.

Only startup/info/shutdown is exercised, not the already validated container
lifecycle/network/storage suites. An optional prepared store is copied privately.
Linux additionally constrains host VA to 2560MiB; Darwin records native RSS/VA.
"""
import hashlib
import json
import os
from pathlib import Path
import platform
import resource
import shutil
import subprocess
import tempfile
import threading
import time
import urllib.request
import m6

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'target/docker-startup-memory'
SCRIPT = '''set -eu
mount -t tmpfs -o mode=755 tmpfs /run
mkdir -p /dev/shm
mount -t tmpfs -o size=64m,mode=1777 tmpfs /dev/shm
daemon=
cleanup() {
    if test -n "$daemon"; then kill "$daemon" 2>/dev/null || :; wait "$daemon" 2>/dev/null || :; fi
    cat /tmp/startup-dockerd.log
}
trap cleanup EXIT
dockerd >/tmp/startup-dockerd.log 2>&1 &
daemon=$!
while ! grep -q 'API listen on' /tmp/startup-dockerd.log; do kill -0 "$daemon"; sleep 1; done
info=$(docker info --format '{{.Driver}} {{.NCPU}} {{.MemTotal}}')
printf 'TRAMES_DOCKER_INFO %s\n' "$info"
test "$info" = 'overlay2 8 4294967296'
kill "$daemon"
wait "$daemon"
daemon=
echo TRAMES_DOCKER_STARTUP_PASS
'''


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    runner = Path(os.getenv('TRAMES_RUNNER', ROOT / 'target/release/trames-run')).resolve()
    report = dict(scope=__doc__, host=platform.system(), arch=platform.machine(),
                  host_page_bytes=os.sysconf('SC_PAGESIZE'), cpus=8,
                  memory_budget_bytes=4 << 30, process_budget_bytes=4 << 30,
                  runner_sha256=sha(runner), test_sha256=sha(Path(__file__)), samples=[])
    base = [str(runner), '--network', 'full', '--cpus', '8', '--memory-budget', '4GiB',
            '--process-memory-budget', '4GiB', '--max-instructions', '8000000000']
    with tempfile.TemporaryDirectory(prefix='trames-startup-memory-') as directory:
        tmp = Path(directory)
        store = tmp / 'store'
        prepared = os.getenv('TRAMES_DOCKER_STARTUP_STORE')
        if prepared:
            shutil.copytree(prepared, store)
            report['store_manifest_sha256'] = sha(store / 'manifest')
        else:
            m6.CACHE.mkdir(parents=True, exist_ok=True)
            archive = m6.CACHE / 'alpine-minirootfs-3.22.6-aarch64.tar.gz'
            if not archive.exists():
                url = 'https://dl-cdn.alpinelinux.org/alpine/v3.22/releases/aarch64/' + archive.name
                with urllib.request.urlopen(url, timeout=120) as source, archive.open('wb') as output:
                    shutil.copyfileobj(source, output)
            assert sha(archive) == '821565fa8f3953eefd12497b166b4b50add2f7c57fb312e75862f5867e06fefe'
            m6.repository(tmp / 'repository', ('docker-engine', 'docker-cli', 'libssl3'))
            packages = [p for p in sorted((tmp / 'repository').rglob('*.apk'))
                        if not p.name.startswith(('dash-', 'yash-'))]
            report['packages_sha256'] = {p.name: sha(p) for p in packages}
            imported = [arg for p in packages for arg in ('--file', '/tmp/' + p.name + '=' + str(p))]
            with (OUT / 'install.log').open('wb') as log:
                subprocess.run([*base, '--storage', str(store), '--rootfs', str(archive), *imported,
                    '--guest', '/sbin/apk', '--repositories-file', '/dev/null', '--no-progress',
                    'add', *['/tmp/' + p.name for p in packages]], stdout=log, stderr=subprocess.STDOUT,
                    check=True, timeout=300)
        stopped = threading.Event()
        began = time.monotonic()
        def constrain():
            resource.setrlimit(resource.RLIMIT_AS, (2560 << 20, 2560 << 20))
        with (OUT / 'boot.log').open('wb') as log:
            process = subprocess.Popen([*base, '--storage', str(store), '--metrics', '--guest',
                '/bin/sh', '-ec', SCRIPT], stdout=log, stderr=subprocess.STDOUT,
                preexec_fn=constrain if platform.system() == 'Linux' else None)
            def sample():
                while not stopped.wait(.5):
                    try:
                        row = dict(seconds=round(time.monotonic()-began, 3))
                        if platform.system() == 'Linux':
                            status = Path(f'/proc/{process.pid}/status').read_text()
                            row.update({l.split(':')[0]: l.split(':')[1].strip() for l in status.splitlines()
                                        if l.startswith(('VmRSS:', 'VmSize:', 'VmPeak:'))})
                            lines = [l for l in Path(f'/proc/{process.pid}/maps').read_text().splitlines()
                                     if 'memfd:trames-ram' in l]
                            row['mmu_mapped_bytes'] = sum(int(l.split()[0].split('-')[1], 16)
                                - int(l.split()[0].split('-')[0], 16) for l in lines)
                        else:
                            values = subprocess.check_output(['ps', '-o', 'rss=,vsz=', '-p', str(process.pid)], text=True).split()
                            row.update(rss_kib=int(values[0]), virtual_kib=int(values[1]))
                        report['samples'].append(row)
                    except (OSError, subprocess.CalledProcessError, ValueError, IndexError):
                        pass
            monitor = threading.Thread(target=sample)
            monitor.start()
            try:
                code = process.wait(timeout=180)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait()
                raise
            finally:
                stopped.set(); monitor.join()
        text = (OUT / 'boot.log').read_text()
        report.update(exit_code=code, seconds=time.monotonic()-began,
                      ready='API listen on' in text,
                      completed=code == 0 and 'TRAMES_DOCKER_STARTUP_PASS' in text,
                      host_va_limit_mib=2560 if platform.system() == 'Linux' else None)
        (OUT / 'results.json').write_text(json.dumps(report, indent=2)+'\n')
        print(json.dumps({k:v for k,v in report.items() if k != 'samples'}, indent=2), flush=True)
        assert report['completed'], text[-6000:]
        if platform.system() == 'Linux':
            assert report['samples'] and max(r.get('mmu_mapped_bytes', 0) for r in report['samples']) <= 1 << 30


if __name__ == '__main__':
    main()
