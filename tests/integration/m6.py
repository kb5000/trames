#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unmodified, Alpine-signed developer tools in a persistent guest store."""
from concurrent.futures import ThreadPoolExecutor
import http.server
import os
from pathlib import Path
import re
import selectors
import shutil
import subprocess
import tarfile
import tempfile
import threading
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
CACHE = Path(os.getenv('TRAMES_ALPINE_CACHE', ROOT / 'target/alpine-download'))
RUNNER = Path(os.getenv('TRAMES_RUNNER', ROOT / 'target/release/trames-run'))
TOOLS = ('git', 'python3', 'build-base', 'clang19', 'cmake', 'htop')


def repository(destination, tools=TOOLS):
    packages, providers = {}, {}
    for name in ('main', 'community'):
        index = CACHE / ('APKINDEX.tar.gz' if name == 'main' else 'APKINDEX-community.tar.gz')
        if not index.exists():
            with urllib.request.urlopen(f'https://dl-cdn.alpinelinux.org/alpine/v3.22/{name}/aarch64/APKINDEX.tar.gz', timeout=120) as response:
                index.write_bytes(response.read())
        repo = destination / name / 'aarch64'
        repo.mkdir(parents=True)
        shutil.copyfile(index, repo / 'APKINDEX.tar.gz')
        with tarfile.open(index) as archive:
            data = archive.extractfile('APKINDEX').read().decode()
        for block in data.split('\n\n'):
            fields = {line[0]: line[2:] for line in block.splitlines() if len(line) > 2 and line[1] == ':'}
            if 'P' not in fields:
                continue
            fields['repository'] = name
            packages.setdefault(fields['P'], fields)
            for provider in fields.get('p', '').split():
                providers.setdefault(re.split('[<>=~]', provider)[0], set()).add(fields['P'])
    selected = set()

    def include(name):
        if name in selected:
            return
        selected.add(name)
        for dependency in packages[name].get('D', '').split():
            if dependency.startswith('!'):
                continue
            dependency = re.split('[<>=~]', dependency)[0]
            if dependency in packages:
                include(dependency)
            else:
                # Let apk select between valid providers; fixture preparation
                # must not silently choose a different one from apk's solver.
                for provider in providers[dependency]:
                    include(provider)

    for name in tools:
        include(name)
    if 'python3' in tools:
        for name in ('pyc', 'python3-pyc', 'python3-pycache-pyc0'):
            include(name)
    if 'git' in tools:
        include('git-init-template')

    def fetch(name):
        fields = packages[name]
        filename = name + '-' + fields['V'] + '.apk'
        cached = CACHE / filename
        if not cached.exists():
            url = f"https://dl-cdn.alpinelinux.org/alpine/v3.22/{fields['repository']}/aarch64/{filename}"
            with urllib.request.urlopen(url, timeout=180) as response:
                with cached.with_suffix('.partial').open('wb') as output:
                    shutil.copyfileobj(response, output)
            cached.with_suffix('.partial').replace(cached)
        shutil.copyfile(cached, destination / fields['repository'] / 'aarch64' / filename)

    with ThreadPoolExecutor(max_workers=4) as workers:
        list(workers.map(fetch, sorted(selected)))
    print('Signed repository inputs:', ', '.join(f"{name}={packages[name]['V']}" for name in tools), flush=True)


def main():
    with tempfile.TemporaryDirectory(prefix='trames-m6-repo-') as directory:
        tmp = Path(directory)
        repository(tmp / 'repo')
        remote = tmp / 'repo/source.git'
        source = tmp / 'source'
        source.mkdir()
        subprocess.run(['git', 'init', '-q', str(source)], check=True)
        (source / 'remote.txt').write_text('HTTP Git transport\n')
        subprocess.run(['git', '-C', str(source), 'add', 'remote.txt'], check=True)
        subprocess.run(['git', '-C', str(source), '-c', 'user.name=Test Peer', '-c', 'user.email=peer@example.invalid', 'commit', '-q', '-m', 'remote input'], check=True)
        subprocess.run(['git', 'clone', '-q', '--bare', str(source), str(remote)], check=True)
        subprocess.run(['git', '-C', str(remote), 'update-server-info'], check=True)

        class Handler(http.server.SimpleHTTPRequestHandler):
            def __init__(self, *args, **kwargs):
                super().__init__(*args, directory=str(tmp / 'repo'), **kwargs)

            def log_message(self, *args):
                pass

        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            store = Path(os.getenv('TRAMES_M6_STORE', tmp / 'store'))
            config = tmp / 'repositories'
            config.write_text(''.join(f'http://198.18.0.1:{server.server_port}/{name}\n' for name in ('main', 'community')))
            base = [str(RUNNER), '--storage', str(store), '--virtual-network', '2', '--host-map', '198.18.0.1=127.0.0.1', '--max-instructions', '100000000000']
            config_path = '/m6-repositories-' + tmp.name

            def run(args, *, timeout=2400, log=None):
                if log is None:
                    process = subprocess.run([*base, *args], capture_output=True, timeout=timeout)
                    out = process.stdout
                else:
                    with log.open('wb') as output:
                        process = subprocess.run([*base, *args], stdout=output, stderr=subprocess.PIPE, timeout=timeout)
                    out = log.read_bytes()
                assert process.returncode == 0, (args, process.returncode, out[-4000:], process.stderr[-4000:])
                return out, process.stderr

            initial = [] if (store / 'manifest').exists() else ['--rootfs', str(CACHE / 'alpine-minirootfs-3.22.6-aarch64.tar.gz')]
            if os.getenv('TRAMES_M6_SKIP_INSTALL') == '1':
                assert not initial, 'Debug reuse requires an existing signed-package store'
                print('Debug reuse of existing tool store; installation not retested', flush=True)
            else:
                out, err = run([*initial, '--file', config_path + '=' + str(config), '--guest', '/sbin/apk', '--repositories-file', config_path, '--no-progress', 'add', *TOOLS])
                assert b'OK:' in out and b'ERROR:' not in out + err, (out[-4000:], err[-4000:])
                print(('Fresh' if initial else 'Reopened') + ' signed developer package installation: passed', flush=True)
            for executable in ('git', 'python3', 'gcc', 'clang-19', 'make', 'cmake'):
                out, err = run(['--guest', '/usr/bin/' + executable, '--version'])
                print(executable, out.splitlines()[0].decode(), flush=True)
            mmap_script = '/mmap-check-' + tmp.name + '.py'
            out, err = run(['--file', mmap_script + '=' + str(ROOT / 'tests/fixtures/mmap-python.py'), '--guest', '/usr/bin/python3', mmap_script])
            assert out == b'python-mmap-sqlite-ok\n' and not err, (out, err)
            print('Python mmap/fork/resize and SQLite mmap workflow: ok', flush=True)
            out, err = run(['--guest', '/bin/sh', '-c', '''
if ! id developer >/dev/null 2>&1; then adduser -D -h /home/developer developer; fi
printf private >/root/m6-private; chmod 600 /root/m6-private
printf 'test "$(id -u)" -ne 0\\n' >/home/developer/check.sh
su - developer -c 'test "$(id -u)" -ne 0; test "$(id -g)" -ne 0; test "$HOME" = /home/developer; test "$(pwd)" = /home/developer; test ! -r /root/m6-private; if cd /root 2>/dev/null; then exit 1; fi; printf owned >owned; /bin/sh /home/developer/check.sh'
test "$(cat /home/developer/owned)" = owned
test "$(stat -c %u /home/developer/owned)" -ne 0
printf 'adduser locks, su identity/home/exec and permissions: ok\\n'
'''])
            assert out == b'adduser locks, su identity/home/exec and permissions: ok\n' and not err, (out, err)
            print(out.decode(), end='', flush=True)
            process = subprocess.Popen([*base, '--pty', '--env', 'TERM=xterm-256color', '--guest', '/usr/bin/htop'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            screen = bytearray()
            with selectors.DefaultSelector() as selector:
                selector.register(process.stdout, selectors.EVENT_READ)
                deadline = time.monotonic() + 30
                while time.monotonic() < deadline and process.poll() is None:
                    if selector.select(0.2):
                        chunk = process.stdout.read1(65536)
                        screen.extend(chunk)
                        if b'Tasks' in screen or b'F1' in screen:
                            break
            if process.poll() is None:
                process.stdin.write(b'q')
                process.stdin.flush()
            try:
                rest, err = process.communicate(timeout=30)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate()
                raise
            screen.extend(rest)
            assert process.returncode == 0 and not err and len(screen) > 500, (process.returncode, screen[-2000:], err)
            print('Unmodified interactive htop: renders processes and exits cleanly', flush=True)
            run(['--guest', '/bin/sh', '-c', 'rm -f /m6-python.py /m6-workflows.sh'])
            out, err = run(['--file', '/m6-python.py=' + str(ROOT / 'tests/fixtures/m6-python.py'), '--file', '/m6-workflows.sh=' + str(ROOT / 'tests/fixtures/m6-workflows.sh'), '--guest', '/bin/sh', '/m6-workflows.sh'], log=ROOT / 'target/m6-workflow.log')
            print(out.decode(), flush=True)
            assert b'python selectors/asyncio/subprocess/threads/sqlite/venv: ok' in out, (out[-4000:], err[-4000:])
            exports = []
            for name in ('gcc-app', 'clang-app'):
                exports += ['--export', '/root/m6/' + name + '=' + str(tmp / name)]
            run([*exports, '--guest', '/bin/true'])
            for name in ('gcc-app', 'clang-app'):
                binary = (tmp / name).read_bytes()
                assert binary[:4] == b'\x7fELF' and int.from_bytes(binary[18:20], 'little') == 183
                # Export deliberately creates a private, non-executable host
                # file. QEMU's ELF loader requires executable permission.
                (tmp / name).chmod(0o700)
                reference = subprocess.run([os.getenv('QEMU_AARCH64', 'qemu-aarch64'), '-L', str(ROOT / 'target/alpine-build/rootfs'), str(tmp / name)], capture_output=True, timeout=30)
                assert reference.returncode == 0 and reference.stdout == b'compiled in guest: 42\n' and not reference.stderr, (name, reference.returncode, reference.stdout, reference.stderr)
            print('Guest-produced GCC/Clang AArch64 ELF binaries independently execute under QEMU', flush=True)
            out, err = run(['--guest', '/bin/sh', '-c', f'rm -rf /root/m6/http-clone; git clone -q http://198.18.0.1:{server.server_port}/source.git /root/m6/http-clone; test "$(cat /root/m6/http-clone/remote.txt)" = "HTTP Git transport"; git -C /root/m6/http-clone fsck --full'])
            assert not err, (out, err)
            print('Actual Git HTTP clone through the virtual egress gateway: passed', flush=True)
            out, err = run(['--guest', '/bin/sh', '-c', 'test "$(cat /root/m6-retained)" = "retained M6 data"; cd /root/m6; ./gcc-app; ./clang-app; git -C clone fsck --full'])
            assert out == b'compiled in guest: 42\ncompiled in guest: 42\n', (out, err)
            print('Reopened store: compiled binaries, Git repository and owner data retained', flush=True)
        finally:
            server.shutdown()
            thread.join()
            server.server_close()


if __name__ == '__main__':
    main()
