#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Default embedding network, host HTTP clients, system DNS and retained config."""
import ctypes
import json
from pathlib import Path
import re
import socket
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
api = ctypes.CDLL(str(ROOT/'target/release'/('libtrames_ffi.dylib' if sys.platform == 'darwin' else 'libtrames_ffi.so')))
api.trames_session_open.argtypes = [ctypes.c_char_p, ctypes.c_uint32]
api.trames_session_open.restype = ctypes.c_uint64
for name in ('trames_session_input', 'trames_session_transfer'):
    getattr(api, name).argtypes = [ctypes.c_uint64, ctypes.c_char_p, ctypes.c_uint32]
    getattr(api, name).restype = ctypes.c_int32
for name in ('trames_session_output', 'trames_session_status'):
    getattr(api, name).argtypes = [ctypes.c_uint64, ctypes.c_void_p, ctypes.c_uint32]
    getattr(api, name).restype = ctypes.c_int32
api.trames_session_close.argtypes = [ctypes.c_uint64]
api.trames_session_close.restype = ctypes.c_int32
api.trames_environment_reset.argtypes = [ctypes.c_char_p, ctypes.c_uint32]
api.trames_environment_reset.restype = ctypes.c_int32
api.trames_last_error.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
api.trames_last_error.restype = ctypes.c_int32

def request(fn, *prefix, payload):
    data = json.dumps(payload).encode()
    return fn(*prefix, data, len(data))

def read(fn, handle):
    buffer = ctypes.create_string_buffer(65536)
    count = fn(handle, buffer, len(buffer))
    assert count >= 0, count
    return buffer.raw[:count]

def status(handle): return json.loads(read(api.trames_session_status, handle))

def collect(handle, predicate):
    output = bytearray(); deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        output.extend(read(api.trames_session_output, handle))
        assert status(handle)['phase'] == 'running', status(handle)
        if predicate(output): return bytes(output)
        time.sleep(.01)
    raise AssertionError(('guest output timeout', output[-3000:]))

def send(handle, command):
    data = (command + '\n').encode()
    assert api.trames_session_input(handle, data, len(data)) == len(data)

with tempfile.TemporaryDirectory(prefix='trames-embedding-network-') as folder:
    options = dict(store=str(Path(folder)/'store'), rootfs=str(ROOT/'target/alpine-download/alpine-minirootfs-3.22.6-aarch64.tar.gz'),
                   volume_total=64 << 30, volume_available=32 << 30)
    for phase in ('fresh', 'managed', 'custom'):
        handle = 0; deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            handle = request(api.trames_session_open, payload=options)
            if not handle:
                buffer = ctypes.create_string_buffer(8192)
                count = api.trames_last_error(buffer, len(buffer))
                raise AssertionError((phase, 'open failed', buffer.raw[:max(0, count)]))
            if handle:
                while status(handle)['phase'] == 'starting' and time.monotonic() < deadline: time.sleep(.01)
                if status(handle)['phase'] == 'running': break
                failure = status(handle)
                assert api.trames_session_close(handle) == 0
                assert failure.get('error') == 'virtual filesystem errno 16', (phase, failure)
            time.sleep(.01)
        else: raise AssertionError('embedding store did not reopen')
        try:
            options.pop('rootfs', None)
            send(handle, "cat /etc/resolv.conf; printf '\\nCONFIG_DONE\\n'")
            output = collect(handle, lambda b: b'\r\nCONFIG_DONE\r\n' in b)
            expected = b'nameserver 192.0.2.53' if phase == 'custom' else b'nameserver 169.254.0.53'
            assert expected + b'\r\n' in output, output
            if phase == 'fresh':
                assert request(api.trames_session_transfer, handle, payload=dict(host=str(ROOT/'tests/fixtures/bin/direct-network'), guest='/root/network', export=False)) == 0
                send(handle, "chmod +x /root/network; /root/network dns; printf '\\nDNS_RESULT:%d\\n' $?")
                output = collect(handle, lambda b: b'\r\nDNS_RESULT:' in b)
                assert b'\r\nDNS_RESULT:0\r\n' in output, output
                send(handle, "/root/network http4; printf '\\nHTTP_RESULT:%d\\n' $?")
                output = collect(handle, lambda b: re.search(rb'\r\nPORT:(\d+)\r\n', b))
                port = int(re.search(rb'\r\nPORT:(\d+)\r\n', output)[1])
                try: address = socket.gethostbyname(socket.gethostname())
                except socket.gaierror: address = '127.0.0.1'
                for host in ('127.0.0.1', address):
                    with socket.create_connection((host, port), timeout=5) as client:
                        client.sendall(b'GET / HTTP/1.1\r\nHost: guest\r\nConnection: close\r\n\r\n')
                        response = bytearray()
                        while chunk := client.recv(4096): response.extend(chunk)
                        assert response.endswith(b'DIRECT_GUEST\n'), response
                output = collect(handle, lambda b: b'\r\nHTTP_RESULT:' in b)
                assert b'\r\nHTTP_RESULT:0\r\n' in output, output
            if phase != 'custom':
                config = 'nameserver 10.0.0.1\\nnameserver fd00::1\\n' if phase == 'fresh' else 'nameserver 192.0.2.53\\n'
                send(handle, "printf '" + config + "' > /etc/resolv.conf; sync; printf '\\nSAVED\\n'")
                collect(handle, lambda b: b'\r\nSAVED\r\n' in b)
        finally:
            assert api.trames_session_close(handle) == 0
    print('Embedding default full native network: real musl DNS and host loopback/LAN HTTP passed')
    print('Retained resolver: former managed gateway replaced; custom configuration preserved')
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        reset = request(api.trames_environment_reset, payload=dict(store=options['store']))
        if reset == 0: break
        assert reset == 1, reset
        time.sleep(.01)
    else: raise AssertionError('embedding worker did not release the test store')
