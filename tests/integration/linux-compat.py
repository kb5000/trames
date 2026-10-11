#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real Alpine tools, signed Vim installation and an interactive edit/save/quit.
No guest wrappers replace ps/ip/ping/apk/Vim. QEMU independently checks musl
threading and SIGEV_THREAD timers. Packet tests use the isolated guest fabric.
"""
import http.server
import os
from pathlib import Path
import selectors
import shutil
import socket
import subprocess
import tarfile
import tempfile
import threading
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / 'target/release/trames-run'
CACHE = Path(os.getenv('TRAMES_ALPINE_CACHE', str(ROOT / 'target/alpine-download')))
ARCHIVE = CACHE / 'alpine-minirootfs-3.22.6-aarch64.tar.gz'

def run(command, timeout=90):
    p = subprocess.run(list(map(str, command)), capture_output=True, timeout=timeout)
    assert p.returncode == 0, (command, p.returncode, p.stdout[-3000:], p.stderr[-3000:])
    return p.stdout, p.stderr

def main():
    count = 0
    with tempfile.TemporaryDirectory(prefix='trames-linux-compat-') as d:
        tmp = Path(d)
        for backend in ('memory', 'disk'):
            storage = ['--memory'] if backend == 'memory' else ['--storage', tmp / 'abi-store']
            for mode in ('threads', 'timers'):
                fixture = ROOT / 'tests/fixtures/bin/linux-compat'
                actual = run([RUNNER, *storage, fixture, mode])
                oracle = run([os.getenv('QEMU_AARCH64', 'qemu-aarch64'), '-cpu', 'cortex-a53', fixture, mode])
                assert actual == oracle and actual[0], (actual, oracle)
                count += 1
        store = tmp / 'alpine'
        base = [RUNNER, '--storage', store, '--virtual-network', '2', '--max-instructions', '2000000000']
        run([*base, '--rootfs', ARCHIVE, '--guest', '/bin/busybox', 'true'])
        def busybox(*args):
            return run([*base, '--guest', '/bin/busybox', *args])
        out, err = busybox('ash', '-c', 'id -u; pwd; printf "%s %s %s\\n" "$HOME" "$USER" "$LOGNAME"; sync')
        assert (out, err) == (b'0\n/root\n/root root root\n', b''), (out, err)
        out, err = busybox('ps')
        assert not err and b'root' in out and b'/bin/busybox ps' in out, (out, err)
        out, err = busybox('mount')
        assert not err and b'/proc' in out and b'/sys' in out and b'/dev/vda1' in out and b'rootfs' in out
        out, err = busybox('df', '-h')
        assert not err and b'/dev/vda1' in out and b'Filesystem' in out
        out, err = busybox('free')
        assert not err and b'Mem:' in out and b'Swap:' in out
        out, err = busybox('top', '-b', '-n', '1')
        assert not err and b'/bin/busybox top' in out and b'PID' in out
        out, err = busybox('ip', 'a')
        assert not err and b'127.0.0.1/8' in out and b'10.0.0.2/24' in out and b'fd00::2/64' in out, (out, err)
        out, err = busybox('ip', 'route')
        assert not err and b'default via 10.0.0.1 dev eth0' in out
        for applet, destination in [('ping', '127.0.0.1'), ('ping6', '::1'), ('ping', '10.0.0.1'), ('ping6', 'fd00::1')]:
            out, err = busybox(applet, '-c', '2', '-W', '1', destination)
            assert not err and b'2 packets received, 0% packet loss' in out, (out, err)
        for applet, destination, native, family, protocol in [('ping', '198.18.0.1', '127.0.0.1', socket.AF_INET, 1), ('ping6', '2001:db8::1', '::1', socket.AF_INET6, 58)]:
            denied = None
            try:
                with socket.socket(family, socket.SOCK_DGRAM, protocol): pass
            except OSError as e:
                denied = e.errno
                assert denied in (1, 13), e
                assert not os.getenv('TRAMES_REQUIRE_ICMP_EGRESS'), e
            p = subprocess.run(list(map(str, [*base, '--host-map', destination + '=' + native, '--guest', '/bin/busybox', applet, '-c', '2', '-W', '1', destination])), capture_output=True, timeout=30)
            if denied:
                assert p.returncode != 0 and (b'Permission denied' in p.stderr or b'Operation not permitted' in p.stderr), (p.returncode, p.stdout, p.stderr)
            else:
                assert p.returncode == 0 and not p.stderr and b'2 packets received, 0% packet loss' in p.stdout, (p.returncode, p.stdout, p.stderr)
            count += 1
        print('Native ICMP egress: real replies or explicit host permission denial verified', flush=True)
        out, err = busybox('ash', '-c', 'head -c 8 /dev/zero | od -An -tx1; head -c 32 /dev/urandom | wc -c; cat /proc/net/dev; cat /sys/class/net/eth0/statistics/tx_packets')
        assert not err and b'00 00 00 00 00 00 00 00' in out and b'32\n' in out and b'eth0:' in out, (out, err)
        count += 12
        # Serve the official index and packages, retaining Alpine signatures.
        repo = tmp / 'repo' / 'aarch64'; repo.mkdir(parents=True)
        index = CACHE / 'APKINDEX.tar.gz'; shutil.copyfile(index, repo / index.name)
        with tarfile.open(index, 'r:gz') as archive:
            text = archive.extractfile('APKINDEX').read().decode()
        packages = {}
        for block in text.split('\n\n'):
            fields = {line[0]: line[2:] for line in block.splitlines() if len(line) > 2 and line[1] == ':'}
            if 'P' in fields: packages[fields['P']] = fields
        for name in ('vim', 'vim-common', 'xxd', 'libncursesw', 'ncurses-terminfo-base'):
            filename = name + '-' + packages[name]['V'] + '.apk'
            cached = CACHE / filename
            if not cached.exists():
                with urllib.request.urlopen('https://dl-cdn.alpinelinux.org/alpine/v3.22/main/aarch64/' + filename, timeout=120) as response:
                    cached.write_bytes(response.read())
            shutil.copyfile(cached, repo / filename)
        class Handler(http.server.SimpleHTTPRequestHandler):
            def __init__(self, *args, **kwargs): super().__init__(*args, directory=str(repo.parent), **kwargs)
            def log_message(self, *args): pass
        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        serving = threading.Thread(target=server.serve_forever, daemon=True); serving.start()
        try:
            repositories = tmp / 'repositories'
            repositories.write_text('http://198.18.0.1:' + str(server.server_port) + '\n')
            # The trusted gateway maps this test address to our real HTTP peer.
            network = ['--host-map', '198.18.0.1=127.0.0.1']
            out, err = run([*base, *network, '--file', '/test-repositories=' + str(repositories), '--guest', '/sbin/apk', '--repositories-file', '/test-repositories', '--no-progress', 'add', 'vim'], timeout=600)
            assert b'OK:' in out and b'errors updating directory permissions' not in out + err, (out[-3000:], err[-3000:])
            print('Fresh signed Alpine apk add vim: OK', flush=True)
        finally:
            server.shutdown(); serving.join(); server.server_close()
        # Keep stdin open while Vim runs, as the actual terminal embedding does.
        output = bytearray(); began = time.monotonic(); sent = False
        with (tmp / 'vim-stderr').open('wb') as errors:
            p = subprocess.Popen(list(map(str, [*base, '--env', 'HOME=/root', '--env', 'TERM=xterm-256color', '--pty', '--guest', '/usr/bin/vim', '/tmp/compat-vim.txt'])), stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=errors)
            poll = selectors.DefaultSelector(); poll.register(p.stdout, selectors.EVENT_READ)
            try:
                while p.poll() is None and time.monotonic() - began < 45:
                    for key, _ in poll.select(.05):
                        data = key.fileobj.read1(65536); output.extend(data)
                        for query, reply in [(b'\x1b[6n', b'\x1b[1;1R'), (b'\x1b[>c', b'\x1b[>0;100;0c'), (b'\x1b[c', b'\x1b[?1;2c')]:
                            if query in data: p.stdin.write(reply); p.stdin.flush()
                    if not sent and time.monotonic() - began > 3 and b'\x1b[?25h' in output:
                        p.stdin.write(b'iTrames Vim integration\x1b:wq\r'); p.stdin.flush(); sent = True
                if p.poll() is None: p.kill()
                p.wait(); output.extend(p.stdout.read())
            finally:
                if p.poll() is None: p.kill(); p.wait()
                poll.close(); p.stdin.close(); p.stdout.close()
        assert p.returncode == 0 and b'E1286' not in output, (p.returncode, output[-3000:], (tmp / 'vim-stderr').read_bytes()[-2000:])
        assert busybox('cat', '/tmp/compat-vim.txt') == (b'Trames Vim integration\n', b'')
        # A new runtime opens the same disk store for the read above.
        count += 2
    print(f'Linux compatibility: {count} checks; ps/mount/df/ip, IPv4/IPv6 ping, devices, signed Vim installation and persistent interactive editing passed', flush=True)

if __name__ == '__main__': main()
