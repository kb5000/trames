#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real embedding timezone installation/resync, musl date and Python DST rules."""
import ctypes
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
LIB = ROOT / 'target/release' / ('libtrames_ffi.dylib' if sys.platform == 'darwin' else 'libtrames_ffi.so')
api = ctypes.CDLL(str(LIB))
api.trames_session_open.argtypes = [ctypes.c_char_p, ctypes.c_uint32]
api.trames_session_open.restype = ctypes.c_uint64
for name in ('input', 'timezone'):
    f = getattr(api, 'trames_session_' + name)
    f.argtypes = [ctypes.c_uint64, ctypes.c_char_p, ctypes.c_uint32]
    f.restype = ctypes.c_int32
for name in ('output', 'status'):
    f = getattr(api, 'trames_session_' + name)
    f.argtypes = [ctypes.c_uint64, ctypes.c_void_p, ctypes.c_uint32]
    f.restype = ctypes.c_int32
api.trames_session_close.argtypes = [ctypes.c_uint64]
api.trames_last_error.argtypes = [ctypes.c_void_p, ctypes.c_uint32]

def error():
    b = ctypes.create_string_buffer(8192)
    n = api.trames_last_error(b, len(b))
    return b.raw[:max(0, n)].decode()

def request(f, *prefix, payload):
    data = json.dumps(payload).encode()
    assert len(data) <= 65536
    return f(*prefix, data, len(data))

def timezone(name):
    data = Path('/usr/share/zoneinfo', name).read_bytes()
    assert data.startswith(b'TZif')
    return {'identifier': name, 'data': list(data)}

def status(h):
    b = ctypes.create_string_buffer(8192)
    n = api.trames_session_status(h, b, len(b))
    assert n > 0, error()
    return json.loads(b.raw[:n])

def command(h, text, expected, timeout=60):
    # A marker assembled from two arguments avoids matching the echoed command.
    data = (text + "; printf '\\n%s%s\\n' TZ_ COMPLETE\n").encode()
    assert api.trames_session_input(h, data, len(data)) == len(data), error()
    output = bytearray()
    b = ctypes.create_string_buffer(65536)
    deadline = time.monotonic() + timeout
    while b'TZ_COMPLETE\r\n' not in output:
        n = api.trames_session_output(h, b, len(b))
        assert n >= 0, error()
        output.extend(b.raw[:n])
        assert time.monotonic() < deadline, (output[-3000:], status(h))
        time.sleep(.01)
    for needle in expected:
        assert needle in output, (needle, output[-3000:])


def main():
    spec = importlib.util.spec_from_file_location('m6_repository', ROOT / 'tests/integration/m6.py')
    m6 = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m6)
    with tempfile.TemporaryDirectory(prefix='trames-timezone-') as folder:
        tmp = Path(folder)
        options = {'store': str(tmp / 'store'), 'rootfs': str(m6.CACHE / 'alpine-minirootfs-3.22.6-aarch64.tar.gz'),
            'volume_total': 64 << 30, 'volume_available': 32 << 30, 'network': 'off', 'timezone': timezone('Asia/Shanghai')}
        if os.getenv('TRAMES_TEST_MUSL'):
            options.update(musl_libc=os.environ['TRAMES_TEST_MUSL'], pure_accelerator=True)
        handle = request(api.trames_session_open, payload=options)
        assert handle, error()
        try:
            deadline = time.monotonic() + 30
            while status(handle)['phase'] == 'starting':
                assert time.monotonic() < deadline
                time.sleep(.01)
            assert status(handle)['phase'] == 'running', status(handle)
            command(handle, "date -d @0 +'%Y-%m-%d %H:%M %z %Z'; cat /etc/timezone",
                [b'1970-01-01 08:00 +0800 CST\r\n', b'Asia/Shanghai\r\n'])
            assert request(api.trames_session_timezone, handle, payload=timezone('America/New_York')) == 0, error()
            command(handle, "date -d @1736942400 +'%z %Z'; date -d @1752580800 +'%z %Z'",
                [b'-0500 EST\r\n', b'-0400 EDT\r\n'])
            command(handle, "TZ=Asia/Shanghai date -d @0 +'%z %Z'; TZ=UTC0 date -d @0 +'%z %Z'",
                [b'+0800 CST\r\n', b'+0000 UTC\r\n'])
            assert request(api.trames_session_timezone, handle, payload={'identifier': '../etc/passwd', 'data': list(b'TZif' + bytes(40))}) == -1
            command(handle, 'cat /etc/timezone', [b'America/New_York\r\n'])
            print('Real FFI initial/resume synchronization, date, historical epoch, DST and explicit TZ: passed', flush=True)
        finally:
            api.trames_session_close(handle)

        m6.repository(tmp / 'repository', ('python3',))
        packages = sorted((tmp / 'repository').rglob('*.apk'))
        runner = os.getenv('TRAMES_RUNNER', str(ROOT / 'target/release/trames-run'))
        base = [runner, '--storage', str(tmp / 'store'), '--max-instructions', '10000000000']
        imported = []
        for package in packages:
            imported += ['--file', '/tmp/' + package.name + '=' + str(package)]
        # Closing a session cancels asynchronously; wait for its store lease.
        deadline = time.monotonic() + 10
        while True:
            install = subprocess.run([*base, *imported, '--guest', '/sbin/apk', '--repositories-file', '/dev/null',
                '--no-progress', 'add', *['/tmp/' + p.name for p in packages]], capture_output=True, timeout=300)
            if b'errno 16' not in install.stderr:
                break
            assert time.monotonic() < deadline, install.stderr
            time.sleep(.05)
        assert install.returncode == 0 and b'ERROR:' not in install.stdout + install.stderr, (install.stdout, install.stderr)
        options.pop('rootfs')
        options['timezone'] = timezone('America/New_York')
        handle = request(api.trames_session_open, payload=options)
        assert handle, error()
        try:
            script = "import time,datetime,os; time.tzset(); assert time.strftime('%z %Z',time.localtime(1736942400))=='-0500 EST'; assert time.strftime('%z %Z',time.localtime(1752580800))=='-0400 EDT'; assert datetime.datetime.fromtimestamp(1736942400).hour==7; os.environ['TZ']='Asia/Shanghai'; time.tzset(); assert time.strftime('%z %Z',time.localtime(0))=='+0800 CST'; print('python-timezone-ok')"
            command(handle, 'python3 -c "' + script + '"', [b'python-timezone-ok\r\n'], timeout=120)
            print('Signed Python localtime/datetime, winter/summer DST, TZ override and store reopen: passed', flush=True)
        finally:
            api.trames_session_close(handle)


if __name__ == '__main__':
    main()
