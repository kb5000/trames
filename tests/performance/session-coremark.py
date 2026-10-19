#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Measure original guest CoreMark through the production terminal session ABI.

Copies an installed Alpine/CoreMark store. Workspace mode includes the real init
shell, host PTY adapter, blocked shell and shared-state polling used by the App.
No guest algorithm, compiler flags or clock is changed. Timing is not a CI gate.
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import tempfile
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--library', type=Path, required=True)
p.add_argument('--store', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--mode', choices=('workspace', 'legacy'), default='workspace')
p.add_argument('--iterations', type=int, default=15000)
p.add_argument('--guest-program', default='/root/coremark/coremark.exe')
p.add_argument('--timeout', type=float, default=240)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
api = C.CDLL(str(a.library.resolve()))
api.trames_session_open.argtypes = [C.c_char_p, C.c_uint32]
api.trames_session_open.restype = C.c_uint64
api.trames_session_close.argtypes = [C.c_uint64]
api.trames_last_error.argtypes = [C.c_void_p, C.c_uint32]
api.trames_session_input.argtypes = [C.c_uint64, C.c_char_p, C.c_uint32]
api.trames_session_terminal_input.argtypes = [C.c_uint64, C.c_uint64, C.c_char_p, C.c_uint32]
for name in ('output', 'status'):
    getattr(api, 'trames_session_' + name).argtypes = [C.c_uint64, C.c_void_p, C.c_uint32]
    getattr(api, 'trames_session_terminal_' + name).argtypes = [C.c_uint64, C.c_uint64, C.c_void_p, C.c_uint32]


def error():
    b = C.create_string_buffer(8192)
    n = api.trames_last_error(b, len(b))
    return b.raw[:max(0, n)].decode()


with tempfile.TemporaryDirectory(prefix='trames-session-coremark-') as tmp:
    store = Path(tmp) / 'store'
    shutil.copytree(a.store, store)
    options = dict(store=str(store), volume_total=16 << 30, volume_available=16 << 30,
                   network='off', wayland=True, multiple_terminals=a.mode == 'workspace',
                   memory_budget_bytes=4 << 30, process_memory_budget_bytes=4 << 30)
    data = json.dumps(options).encode()
    handle = api.trames_session_open(data, len(data))
    assert handle, error()
    output = bytearray()

    def receive(kind, terminal=False):
        b = C.create_string_buffer(65536)
        fn = getattr(api, 'trames_session_' + ('terminal_' if terminal else '') + kind)
        ids = (handle, 1) if terminal else (handle,)
        n = fn(*ids, b, len(b))
        assert n >= 0, error()
        return b.raw[:n]

    def wait_for(marker):
        end = time.monotonic() + a.timeout
        while time.monotonic() < end:
            output.extend(receive('output', a.mode == 'workspace'))
            if marker in output:
                return
            status = json.loads(receive('status'))
            assert status['phase'] not in ('failed', 'exited', 'cancelled'), (status, output[-2000:])
            time.sleep(.005)
        raise TimeoutError(bytes(output[-2000:]))

    def send(command):
        command = command.encode()
        ids = (handle, 1) if a.mode == 'workspace' else (handle,)
        fn = api.trames_session_terminal_input if a.mode == 'workspace' else api.trames_session_input
        assert fn(*ids, command, len(command)) == len(command), error()

    try:
        wait_for(b'# ')
        send("stty -echo; printf '\\nSESSION_COREMARK_READY\\n'\n")
        wait_for(b'\r\nSESSION_COREMARK_READY\r\n')
        output.clear()
        started = time.monotonic()
        send(f'{a.guest_program} 0 0 0x66 {a.iterations} 7 1 2000; printf "\\nSESSION_COREMARK_DONE:%s\\n" "$?"\n')
        wait_for(b'\r\nSESSION_COREMARK_DONE:0\r\n')
        wall = time.monotonic() - started
        text = output.decode(errors='replace')
        assert 'Correct operation validated.' in text
        seconds = float(re.search(r'Total time \(secs\):\s*([0-9.]+)', text)[1])
        score = float(re.search(r'Iterations/Sec\s*:\s*([0-9.]+)', text)[1])
        assert seconds >= 10, 'Increase --iterations for a valid CoreMark duration'
        report = dict(mode=a.mode, host=platform.machine(), seconds=seconds, score=score,
                      wall_seconds=wall, iterations=a.iterations, crc_passed=True,
                      library_sha256=hashlib.sha256(a.library.read_bytes()).hexdigest(),
                      input_store_manifest_sha256=hashlib.sha256((a.store / 'manifest').read_bytes()).hexdigest(),
                      scope=__doc__)
        (a.output / 'stdout.log').write_text(text)
        (a.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2), flush=True)
    finally:
        api.trames_session_close(handle)
