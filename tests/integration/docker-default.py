#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Signed original Docker defaults: managed containerd, runc, overlay2, no tuning.

Only a controlled HTTP peer/client runs on the host; no host Docker or mounts.
No GOMAXPROCS, default-runtime, external containerd, nofile or security overrides.
"""
import gzip
import hashlib
import http.server
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import threading
import time
import m6

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'target/docker-default-acceptance'
OUT.mkdir(parents=True, exist_ok=True)
RUNNER = Path(os.getenv('TRAMES_RUNNER', ROOT / 'target/release/trames-run'))
SHELL = ROOT / 'tests/integration/docker-default.sh'
FIXTURE = ROOT / 'tests/fixtures/bin/socket-timeouts'


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        data = b'TRAMES_DEFAULT_EGRESS\n'
        self.send_response(200)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *unused):
        pass


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
result = {'scope': __doc__, 'cpus': 4, 'inputs_sha256': {
    'runner': hashlib.sha256(RUNNER.read_bytes()).hexdigest(),
    'shell': hashlib.sha256(SHELL.read_bytes()).hexdigest(),
    'runtime_probe': hashlib.sha256(FIXTURE.read_bytes()).hexdigest(),
    'runtime_probe_source': hashlib.sha256((ROOT / 'tests/fixtures/socket-timeouts.c').read_bytes()).hexdigest()}, 'host_browser': []}
try:
    with tempfile.TemporaryDirectory(prefix='trames-default-docker-') as directory:
        tmp = Path(directory)
        m6.repository(tmp / 'repository', ('docker-engine', 'docker-cli', 'libssl3'))
        packages = [p for p in sorted((tmp / 'repository').rglob('*.apk'))
                    if not p.name.startswith(('dash-', 'yash-'))]
        assert any(p.name.startswith('runc-') for p in packages)
        assert not any(p.name.startswith('crun-') for p in packages)
        result['packages_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in packages}
        rootfs = m6.CACHE / 'alpine-minirootfs-3.22.6-aarch64.tar.gz'
        result['rootfs_sha256'] = hashlib.sha256(rootfs.read_bytes()).hexdigest()
        with gzip.open(rootfs, 'rb') as source, (tmp / 'alpine.tar').open('wb') as dest:
            shutil.copyfileobj(source, dest)
        with socket.socket() as reserve:
            reserve.bind(('127.0.0.1', 0))
            published = reserve.getsockname()[1]
        script = SHELL.read_text().replace('@PUBLISHED@', str(published)).replace('@HTTP@', str(server.server_port))
        base = [str(RUNNER), '--storage', str(tmp / 'store'), '--network', 'full', '--cpus', '4',
                '--max-instructions', '50000000000']
        imported = [arg for p in packages for arg in ('--file', '/tmp/' + p.name + '=' + str(p))]
        install = subprocess.run([*base, '--rootfs', str(rootfs), *imported,
            '--exec-file', '/runtime-probe=' + str(FIXTURE),
            '--file', '/tmp/alpine-rootfs.tar=' + str(tmp / 'alpine.tar'), '--guest', '/sbin/apk',
            '--repositories-file', '/dev/null', '--no-progress', 'add', *['/tmp/' + p.name for p in packages]],
            capture_output=True, timeout=300)
        (OUT / 'install.log').write_bytes(install.stdout + install.stderr)
        assert install.returncode == 0 and b'ERROR:' not in install.stdout + install.stderr
        logpath = OUT / 'default.log'
        logpath.write_bytes(b'')
        stop = threading.Event()

        def progress():
            offset = 0
            while not stop.wait(1):
                with logpath.open('rb') as current:
                    current.seek(offset)
                    data = current.read()
                    offset = current.tell()
                if data:
                    print(data.decode(errors='replace'), end='', flush=True)

        threading.Thread(target=progress, daemon=True).start()

        def browser():
            while not stop.wait(.1):
                if logpath.exists() and '\nHOST_DEFAULT_PUBLISHED_READY\n' in logpath.read_text(errors='replace'):
                    break
            if stop.is_set():
                return
            try:
                deadline = time.monotonic() + 60
                while True:
                    try:
                        with socket.create_connection(('127.0.0.1', published), timeout=15) as client:
                            client.settimeout(15)
                            client.sendall(b'GET / HTTP/1.0\r\n\r\n')
                            data = b''
                            while b'\r\n\r\nTRAMES_DEFAULT_RUNC_OK\n' not in data:
                                chunk = client.recv(4096)
                                if not chunk:
                                    raise ConnectionError('Published listener not ready')
                                data += chunk
                        result['host_browser'].append({'tcp': True})
                        print('HOST_DEFAULT_PUBLISHED_TCP_PASS', flush=True)
                        break
                    except (OSError, ConnectionError):
                        if time.monotonic() >= deadline:
                            raise
                        time.sleep(.1)
            except Exception as error:
                result['host_browser'].append({'error': str(error)})

        worker = threading.Thread(target=browser, daemon=True)
        worker.start()
        started = time.monotonic()
        try:
            with logpath.open('wb') as log:
                process = subprocess.run([*base, '--host-map', '198.18.0.1=127.0.0.1',
                    '--guest', '/bin/sh', '-ec', script], stdout=log, stderr=subprocess.STDOUT, timeout=1800)
        except subprocess.TimeoutExpired:
            result['timed_out'] = True
            process = subprocess.CompletedProcess([], 124)
        finally:
            stop.set()
            worker.join(timeout=20)
        output = logpath.read_text()
        result.update(exit_code=process.returncode, seconds=round(time.monotonic() - started, 3),
            checks={name: ('\nDOCKER_DEFAULT_' + name + '_PASS\n') in output
                    for name in ('DAEMON', 'PAYLOAD', 'EXEC', 'OVERLAY', 'RESTART', 'CLEANUP')}, output=output)
        (OUT / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
        assert process.returncode == 0 and all(result['checks'].values()), output[-14000:]
        assert result['host_browser'] == [{'tcp': True}], result['host_browser']
        print('Original Docker defaults: managed containerd, runc, overlay2, exec, DNS/egress/publish, restart passed', flush=True)
finally:
    server.shutdown()
print(f'Evidence: {OUT}')
