#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real AArch64/musl sockets against deterministic native loopback peers."""
import contextlib
import os
import pathlib
import select
import socket
import struct
import subprocess
import tempfile
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
RUNNER = ROOT / 'target/debug/trames-run'
FIXTURE = ROOT / 'tests/fixtures/bin/network'
QEMU = os.environ.get('QEMU_AARCH64', 'qemu-aarch64')


def result(command, expected=0):
    p = subprocess.run(list(map(str, command)), capture_output=True, timeout=30)
    status = p.returncode if p.returncode >= 0 else 128 - p.returncode
    value = status, p.stdout, p.stderr
    if status != expected:
        raise AssertionError((command, value))
    return value


@contextlib.contextmanager
def echo_server(kind, ipv6=False):
    family = socket.AF_INET6 if ipv6 else socket.AF_INET
    server = socket.socket(family, kind)
    server.settimeout(5)
    host = '::1' if ipv6 else '127.0.0.1'
    server.bind((host, 0))
    port = server.getsockname()[1]
    if kind == socket.SOCK_STREAM:
        server.listen(2)
    failures, packets = [], []

    def serve():
        try:
            if kind == socket.SOCK_DGRAM:
                for _ in range(3):
                    data, peer = server.recvfrom(65536)
                    packets.append(data)
                    server.sendto(data, peer)
            else:
                stream, _ = server.accept()
                with stream:
                    stream.settimeout(5)
                    time.sleep(0.1)
                    data = bytearray()
                    while True:
                        block = stream.recv(4096)
                        if not block:
                            break
                        data.extend(block)
                        if len(data) > 256 * 1024:
                            raise AssertionError('unbounded fixture stream')
                    packets.append(bytes(data))
                    for offset in range(0, len(data), 4096):
                        stream.sendall(data[offset:offset+4096])
        except Exception as e:
            failures.append(e)

    worker = threading.Thread(target=serve, daemon=True)
    worker.start()
    succeeded = False
    try:
        yield port
        succeeded = True
    finally:
        server.close()
        worker.join(timeout=6)
    if succeeded:
        if worker.is_alive() or failures:
            raise AssertionError(('echo server', failures))
        if kind == socket.SOCK_DGRAM and packets != [b'a\0b:udp', b'', b'end']:
            raise AssertionError(('datagrams changed', packets))


class DnsServer:
    """A/AAAA responses, optionally force real musl UDP-to-TCP fallback."""
    def __init__(self, fallback=False):
        self.fallback = fallback
        self.stop = threading.Event()
        self.errors = []
        self.queries = []
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.tcp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            self.udp.bind(('127.0.0.1', 53))
            self.tcp.bind(('127.0.0.1', 53))
            self.tcp.listen(8)
        except Exception:
            self.udp.close()
            self.tcp.close()
            raise
        for s in (self.udp, self.tcp):
            s.settimeout(0.1)
        self.workers = [threading.Thread(target=f, daemon=True) for f in (self.serve_udp, self.serve_tcp)]
        for w in self.workers:
            w.start()

    def answer(self, query, tcp=False):
        if len(query) < 12:
            raise AssertionError('short DNS query')
        offset, labels = 12, []
        while query[offset]:
            n = query[offset]
            labels.append(query[offset+1:offset+1+n])
            offset += n+1
        offset += 1
        kind, cls = struct.unpack_from('!HH', query, offset)
        if b'.'.join(labels) != b'trames.test' or kind not in (1, 28) or cls != 1:
            raise AssertionError(('unexpected DNS question', labels, kind, cls))
        self.queries.append((tcp, kind))
        question = query[12:offset+4]
        if self.fallback and not tcp:
            return query[:2] + struct.pack('!HHHHH', 0x8380, 1, 0, 0, 0) + question
        address = socket.inet_pton(socket.AF_INET if kind == 1 else socket.AF_INET6,
                                   '127.0.0.42' if kind == 1 else '::1')
        record = b'\xc0\x0c' + struct.pack('!HHIH', kind, 1, 30, len(address)) + address
        return query[:2] + struct.pack('!HHHHH', 0x8180, 1, 1, 0, 0) + question + record

    def serve_udp(self):
        while not self.stop.is_set():
            try:
                data, peer = self.udp.recvfrom(4096)
                self.udp.sendto(self.answer(data), peer)
            except socket.timeout:
                pass
            except OSError as e:
                if not self.stop.is_set():
                    self.errors.append(e)
                return
            except Exception as e:
                self.errors.append(e)
                return

    @staticmethod
    def read_exact(stream, count):
        data = bytearray()
        while len(data) < count:
            block = stream.recv(count - len(data))
            if not block:
                raise AssertionError('incomplete TCP DNS message')
            data.extend(block)
        return bytes(data)

    def serve_tcp(self):
        while not self.stop.is_set():
            try:
                stream, _ = self.tcp.accept()
                with stream:
                    stream.settimeout(3)
                    length = struct.unpack('!H', self.read_exact(stream, 2))[0]
                    query = self.read_exact(stream, length)
                    answer = self.answer(query, tcp=True)
                    # Fragment the length prefix and DNS packet.
                    packet = struct.pack('!H', len(answer)) + answer
                    stream.sendall(packet[:1])
                    time.sleep(0.005)
                    stream.sendall(packet[1:])
            except socket.timeout:
                pass
            except OSError as e:
                if not self.stop.is_set():
                    self.errors.append(e)
                return
            except Exception as e:
                self.errors.append(e)
                return

    def close(self):
        self.stop.set()
        self.udp.close()
        self.tcp.close()
        for w in self.workers:
            w.join(timeout=4)
        if self.errors:
            raise AssertionError(('DNS server', self.errors))
        for tcp in ([False, True] if self.fallback else [False]):
            if {kind for transport, kind in self.queries if transport == tcp} != {1, 28}:
                raise AssertionError(('DNS transports not exercised', self.queries))


def host_client(command, ipv6):
    p = subprocess.Popen(list(map(str, command)), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        if not select.select([p.stdout], [], [], 15)[0]:
            raise AssertionError('guest listener did not become ready')
        line = p.stdout.readline()
        if not line.startswith(b'PORT '):
            raise AssertionError(('guest listener', line, p.communicate(timeout=3)))
        port = int(line[5:])
        family = socket.AF_INET6 if ipv6 else socket.AF_INET
        with socket.socket(family, socket.SOCK_STREAM) as stream:
            stream.settimeout(5)
            stream.connect(('::1' if ipv6 else '127.0.0.1', port))
            time.sleep(0.1)
            stream.sendall(b'host')
            data = DnsServer.read_exact(stream, 4)
            if data != b'host':
                raise AssertionError(('guest listener changed bytes', data))
        out, err = p.communicate(timeout=15)
        if p.returncode != 0:
            raise AssertionError(('guest listener result', p.returncode, out, err))
        return p.returncode, out, err
    finally:
        if p.poll() is None:
            p.kill()
            p.wait(timeout=5)
        p.stdout.close()
        p.stderr.close()


def main():
    comparisons = 0
    with tempfile.TemporaryDirectory(prefix='trames-network-') as directory:
        folder = pathlib.Path(directory)
        for backend in ('memory', 'disk'):
            options = ['--memory'] if backend == 'memory' else ['--storage', folder/backend]
            base = [RUNNER, *options, '--max-instructions', '100000000']
            reference = [QEMU, '-cpu', 'cortex-a53', FIXTURE]
            for ipv6 in (False, True):
                family_arg = '6' if ipv6 else '4'
                for mode, kind in [('udp', socket.SOCK_DGRAM), ('tcp', socket.SOCK_STREAM)]:
                    values = []
                    for prefix in ([*base, FIXTURE], reference):
                        with echo_server(kind, ipv6) as port:
                            values.append(result([*prefix, mode, port, family_arg]))
                    if values[0] != values[1]:
                        raise AssertionError((backend, mode, ipv6, values))
                    comparisons += 1
                    if mode == 'udp':
                        with echo_server(kind, ipv6) as port:
                            result([*base, FIXTURE, 'udp-preflight', port, family_arg])
                with socket.socket(socket.AF_INET6 if ipv6 else socket.AF_INET, socket.SOCK_STREAM) as reserved:
                    reserved.bind(('::1' if ipv6 else '127.0.0.1', 0))
                    port = reserved.getsockname()[1]
                    for mode in ('refused', 'refused-blocking'):
                        actual = result([*base, FIXTURE, mode, port, family_arg])
                        oracle = result([*reference, mode, port, family_arg])
                        if actual != oracle:
                            raise AssertionError((mode, actual, oracle))
                        comparisons += 1
                for mode in ('fork-server',):
                    actual = result([*base, FIXTURE, mode, '0', family_arg])
                    oracle = result([*reference, mode, '0', family_arg])
                    if actual != oracle:
                        raise AssertionError((mode, actual, oracle))
                    comparisons += 1
                actual = host_client([*base, FIXTURE, 'host-server', '0', family_arg], ipv6)
                oracle = host_client([*reference, 'host-server', '0', family_arg], ipv6)
                if actual != oracle:
                    raise AssertionError(('host-server', actual, oracle))
                comparisons += 1
            for mode in ('eintr', 'restart', 'socket-abi'):
                actual = result([*base, FIXTURE, mode])
                oracle = result([*reference, mode])
                if actual != oracle:
                    raise AssertionError((mode, actual, oracle))
                comparisons += 1
            values = []
            for prefix in ([*base, FIXTURE], reference):
                with echo_server(socket.SOCK_STREAM) as port:
                    values.append(result([*prefix, 'sigpipe', port], expected=141))
            if values[0] != values[1]:
                raise AssertionError(('SIGPIPE', values))
            comparisons += 1
            for mode in ('cancel', 'cancel-fault'):
                with echo_server(socket.SOCK_STREAM) as port:
                    outcome = result([*base, FIXTURE, mode, port],
                                     expected=132 if mode == 'cancel-fault' else 0)
                    if mode == 'cancel' and outcome[1:] != (b'cancel-ok\n', b''):
                        raise AssertionError(('session cancellation', outcome))
                    if mode == 'cancel-fault' and b'unsupported or unallocated instruction' not in outcome[2]:
                        raise AssertionError(('session fault', outcome))
            for mode in ('off', 'loopback', 'outbound', 'full'):
                actual = result([*base, '--network', mode, FIXTURE, 'policy', mode])
                if actual[1:] != (b'policy-ok\n', b''):
                    raise AssertionError(('network policy', mode, actual))
            resolv = folder/'resolv.conf'
            resolv.write_text('nameserver 127.0.0.1\noptions timeout:1 attempts:1\n')
            # Disk imports must not overwrite an existing VFS file on the next DNS run.
            for fallback in (False, True):
                dns_options = ['--memory'] if backend == 'memory' else ['--storage', folder/f'dns-{fallback}']
                server = DnsServer(fallback)
                try:
                    actual = result([RUNNER, *dns_options, '--file', f'/etc/resolv.conf={resolv}',
                                     '--max-instructions', '100000000', FIXTURE, 'dns'])
                finally:
                    server.close()
                # QEMU's -L prefix scopes the unmodified musl resolver's config.
                root = folder/f'oracle-dns-{backend}-{fallback}'
                (root/'etc').mkdir(parents=True)
                (root/'etc/resolv.conf').write_text(resolv.read_text())
                server = DnsServer(fallback)
                try:
                    oracle = result([QEMU, '-cpu', 'cortex-a53', '-L', root, FIXTURE, 'dns'])
                finally:
                    server.close()
                if actual != oracle:
                    raise AssertionError(('musl DNS', fallback, actual, oracle))
                comparisons += 1
    print(f'Network integration passed: {comparisons} QEMU comparisons across memory/disk; '
          'IPv4/IPv6 TCP/UDP, preflight, binary payloads, short IO, half-close, '
          'fork/listen, host peers, refused connections, EINTR/restart, SIGPIPE and '
          'real musl DNS UDP/TCP fallback; 8 network policy, 4 receive preflight and '
          '4 session teardown outcomes')


if __name__ == '__main__':
    main()
