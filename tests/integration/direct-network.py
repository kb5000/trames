#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Host HTTP/UDP clients reach native guest listeners without an IP gateway."""
import os
from pathlib import Path
import selectors
import socket
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
RUNNER = os.environ.get('TRAMES_RUNNER', str(ROOT/'target/release/trames-run'))
FIXTURE = ROOT/'tests/fixtures/bin/direct-network'
try:
    LAN = socket.gethostbyname(socket.gethostname())
except socket.gaierror:
    LAN = '127.0.0.1'
with tempfile.TemporaryDirectory(prefix='trames-direct-network-') as folder:
    for cpus in ('1', '4'):
        base = [RUNNER, '--storage', str(Path(folder)/cpus), '--cpus', cpus, '--max-instructions', '1000000000']
        icmp_permissions = {}
        result = subprocess.run([*base, str(FIXTURE), 'dns'], capture_output=True, timeout=30)
        assert result.returncode == 0 and b'musl getaddrinfo: ok\n' in result.stdout, (result.returncode, result.stdout, result.stderr)
        for mode, family, protocol in [('icmp4', socket.AF_INET, 1), ('icmp6', socket.AF_INET6, 58)]:
            expected = 0
            try:
                with socket.socket(family, socket.SOCK_DGRAM, protocol): pass
            except PermissionError:
                assert not os.environ.get('TRAMES_REQUIRE_ICMP_EGRESS'), 'host ICMP datagrams required by this run'
                expected = 77
            result = subprocess.run([*base, str(FIXTURE), mode], capture_output=True, timeout=15)
            assert result.returncode == expected, (mode, result.returncode, result.stdout, result.stderr)
            icmp_permissions[family] = expected == 0
            print(f'{cpus}-CPU {mode}: {"native reply verified" if expected == 0 else "host permission denial propagated (echo untested)"}', flush=True)
        for mode in ('http4', 'http6', 'udp'):
            with subprocess.Popen([*base, str(FIXTURE), mode], stdout=subprocess.PIPE, stderr=subprocess.PIPE) as process:
                try:
                    with selectors.DefaultSelector() as selector:
                        selector.register(process.stdout, selectors.EVENT_READ)
                        assert selector.select(15), ('listener startup timeout', cpus, mode)
                    line = process.stdout.readline()
                    assert line.startswith(b'PORT:'), (line, process.poll())
                    port = int(line[5:])
                    if mode == 'udp':
                        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
                            client.settimeout(5); client.sendto(b'DIRECT_UDP', (LAN, port))
                            assert client.recvfrom(100)[0] == b'DIRECT_UDP'
                    else:
                        for host in (['::1', '::1'] if mode == 'http6' else ['127.0.0.1', LAN]):
                            with socket.create_connection((host, port), timeout=5) as client:
                                client.sendall(b'GET / HTTP/1.1\r\nHost: guest\r\nConnection: close\r\n\r\n')
                                response = bytearray()
                                while chunk := client.recv(4096): response.extend(chunk)
                                assert response.startswith(b'HTTP/1.1 200 OK\r\n') and response.endswith(b'DIRECT_GUEST\n'), response
                    output, error = process.communicate(timeout=10)
                    assert process.returncode == 0 and b'direct inbound listener: ok' in output, (output, error)
                finally:
                    if process.poll() is None: process.kill(); process.wait()
        print(f'{cpus}-CPU native DNS, IPv4/IPv6 HTTP and UDP inbound passed; IPv4 host address {LAN}', flush=True)
        alpine = [RUNNER, '--storage', str(Path(folder)/('alpine-'+cpus)), '--cpus', cpus,
                  '--max-instructions', '200000000']
        result = subprocess.run([*alpine, '--rootfs', str(ROOT/'target/alpine-download/alpine-minirootfs-3.22.6-aarch64.tar.gz'),
                                 '--guest', '/bin/busybox', 'ip', 'a'], capture_output=True, timeout=30)
        assert result.returncode == 0 and b'127.0.0.1/8' in result.stdout and not result.stderr, (result.stdout, result.stderr)
        if LAN != '127.0.0.1': assert (LAN+'/').encode() in result.stdout, result.stdout
        for applet, family, destination in [('ping', socket.AF_INET, '127.0.0.1'), ('ping6', socket.AF_INET6, '::1')]:
            result = subprocess.run([*alpine, '--guest', '/bin/busybox', applet, '-c', '1', '-W', '1', destination], capture_output=True, timeout=15)
            if icmp_permissions[family]:
                assert result.returncode == 0 and b'1 packets received, 0% packet loss' in result.stdout, (applet, result.stdout, result.stderr)
            else:
                assert result.returncode != 0 and (b'Permission denied' in result.stderr or b'Operation not permitted' in result.stderr), (applet, result.stdout, result.stderr)
        print(f'{cpus}-CPU real BusyBox ip a and ping/ping6 permission/reply behavior passed', flush=True)
