#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Cold-process app ABI reopen with persisted Vulkan ICD and unchanged user data.

No native GPU is needed: startup must not call the driver until a guest submits.
"""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
LIBRARY = ROOT / 'target/release' / ('libtrames_ffi.dylib' if sys.platform == 'darwin' else 'libtrames_ffi.so')


def boot(store, icd, first, enabled):
    api = ctypes.CDLL(str(LIBRARY))
    api.trames_session_open.argtypes = [ctypes.c_char_p, ctypes.c_uint32]
    api.trames_session_open.restype = ctypes.c_uint64
    api.trames_session_input.argtypes = [ctypes.c_uint64, ctypes.c_char_p, ctypes.c_uint32]
    api.trames_session_close.argtypes = [ctypes.c_uint64]
    for name in ('trames_session_status', 'trames_session_output'):
        getattr(api, name).argtypes = [ctypes.c_uint64, ctypes.c_void_p, ctypes.c_uint32]
    api.trames_last_error.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    options = dict(store=str(store), volume_total=16 << 30, volume_available=16 << 30,
                   network='off', vulkan_accelerator=enabled, vulkan_icd=str(icd))
    if first:
        options['rootfs'] = str(ROOT / 'target/alpine-download/alpine-minirootfs-3.22.6-aarch64.tar.gz')
    payload = json.dumps(options).encode()
    handle = api.trames_session_open(payload, len(payload))
    buffer = ctypes.create_string_buffer(65536)
    if not handle:
        n = api.trames_last_error(buffer, len(buffer))
        raise AssertionError(buffer.raw[:max(n, 0)])

    def status():
        n = api.trames_session_status(handle, buffer, len(buffer))
        assert n > 0, n
        return json.loads(buffer.raw[:n])

    deadline = time.monotonic() + 45
    try:
        while True:
            state = status()
            if state['phase'] == 'running':
                break
            assert state['phase'] == 'starting' and time.monotonic() < deadline, state
            time.sleep(.01)
        command = ("printf 'reopen-data\\n' > /root/reopen-marker; " if first else
                   "test \"$(cat /root/reopen-marker)\" = reopen-data || exit 81; ")
        command += ("printf 'ICD_HASH:%s\\n' \"$(sha256sum /usr/lib/libtrames_vulkan_icd.so | cut -d' ' -f1)\"; "
                    "test -s /usr/share/vulkan/icd.d/trames.json || exit 82; exit\n")
        data = command.encode()
        assert api.trames_session_input(handle, data, len(data)) == len(data)
        output = bytearray()
        while True:
            n = api.trames_session_output(handle, buffer, len(buffer))
            assert n >= 0, n
            output.extend(buffer.raw[:n])
            state = status()
            if state['phase'] == 'exited':
                assert state['exit_code'] == 0, (state, output[-2000:])
                n = api.trames_session_output(handle, buffer, len(buffer))
                assert n >= 0, n
                output.extend(buffer.raw[:n])
                break
            assert state['phase'] == 'running' and time.monotonic() < deadline, (state, output[-2000:])
            time.sleep(.01)
        expected = b'ICD_HASH:' + hashlib.sha256(icd.read_bytes()).hexdigest().encode() + b'\r\n'
        assert expected in output, output[-2000:]
    finally:
        assert api.trames_session_close(handle) == 0


if __name__ == '__main__':
    if len(sys.argv) > 1:
        boot(Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3] == 'first', sys.argv[4] == 'enabled')
    else:
        with tempfile.TemporaryDirectory(prefix='trames-embedding-reopen-') as directory:
            tmp = Path(directory)
            icd = tmp / 'bundled-icd.so'
            shutil.copyfile(ROOT / 'target/vulkan-guest/libtrames_vulkan_icd.so', icd)
            for phase in ('first', 'second', 'updated', 'disabled', 'enabled-again'):
                if phase == 'updated':
                    with icd.open('ab') as file:
                        file.write(b'\0updated-bundle')
                result = subprocess.run([sys.executable, str(Path(__file__).resolve()), str(tmp / 'store'),
                                         str(icd), phase, 'disabled' if phase == 'disabled' else 'enabled'],
                                        capture_output=True, timeout=60)
                assert result.returncode == 0, (phase, result.stdout, result.stderr)
                print(f'PASS cold embedding boot {phase}: preserved user data and exact bundled ICD bytes', flush=True)
