#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Original Alpine Node REPL through the production App's multi-terminal ABI."""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import resource
import re
import subprocess
import sys
import time

from m6 import CACHE, ROOT, repository

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--store', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--ffi', type=Path, default=ROOT/'target/release'/('libtrames_ffi.dylib' if sys.platform == 'darwin' else 'libtrames_ffi.so'))
p.add_argument('--musl', type=Path, default=ROOT/'target/musl-hybrid/libc.so')
p.add_argument('--prepare', action='store_true', help='install signed nodejs into a fresh isolated store')
p.add_argument('--expected-host-page', type=int)
a = p.parse_args()
a.store = a.store.resolve(); a.output = a.output.resolve()
a.output.mkdir(parents=True, exist_ok=False)
if a.expected_host_page:
    assert resource.getpagesize() == a.expected_host_page, resource.getpagesize()
if hasattr(os, 'sched_setaffinity'):
    os.sched_setaffinity(0, sorted(os.sched_getaffinity(0))[:4])
if a.prepare:
    assert not a.store.exists(), 'preparation requires a fresh isolated store'
    repo = a.output/'repository'
    repository(repo, tools=('nodejs',))
    archive = CACHE/'alpine-minirootfs-3.22.6-aarch64.tar.gz'
    command = [str(ROOT/'target/release/trames-run'), '--storage', str(a.store),
               '--rootfs', str(archive), '--network', 'off', '--cpus', '4',
               '--memory-budget', '4GiB', '--process-memory-budget', '4GiB',
               '--max-instructions', '100000000000',
               '--accelerator', 'pure', '--musl-libc', str(a.musl.resolve())]
    for f in sorted(repo.rglob('*')):
        if f.is_file():
            command += ['--file', '/tmp/node-repl-repo/'+f.relative_to(repo).as_posix()+'='+str(f)]
    command += ['--guest', '/bin/sh', '-ec',
                "printf '/tmp/node-repl-repo/main\\n/tmp/node-repl-repo/community\\n' >/tmp/node-repl.repositories; "
                'apk --repositories-file /tmp/node-repl.repositories --no-progress add nodejs; '
                'node --version; rm -rf /tmp/node-repl-repo']
    with (a.output/'install.stdout').open('wb') as so, (a.output/'install.stderr').open('wb') as se:
        subprocess.run(command, stdout=so, stderr=se, timeout=300, check=True)

api = C.CDLL(str(a.ffi.resolve()))
api.trames_session_open.argtypes = [C.c_char_p, C.c_uint32]
api.trames_session_open.restype = C.c_uint64
api.trames_session_terminal_input.argtypes = [C.c_uint64, C.c_uint64, C.c_char_p, C.c_uint32]
api.trames_session_terminal_output.argtypes = [C.c_uint64, C.c_uint64, C.c_void_p, C.c_uint32]
api.trames_session_terminal_open.argtypes = [C.c_uint64]
api.trames_session_terminal_open.restype = C.c_uint64
api.trames_session_status.argtypes = [C.c_uint64, C.c_void_p, C.c_uint32]
api.trames_session_close.argtypes = [C.c_uint64]
options = dict(store=str(a.store), volume_total=16<<30, volume_available=16<<30,
               network='off', wayland=True, multiple_terminals=True, pure_accelerator=True,
               musl_libc=str(a.musl.resolve()), memory_budget_bytes=4<<30,
               process_memory_budget_bytes=4<<30)
b = json.dumps(options).encode()
h = api.trames_session_open(b, len(b)); assert h, 'session open failed'
logs = {}; queries = {}; status = {}; checks = []
report = dict(options=options, platform=sys.platform, host_page_bytes=resource.getpagesize(),
              ffi_sha256=hashlib.sha256(a.ffi.read_bytes()).hexdigest(),
              musl_sha256=hashlib.sha256(a.musl.read_bytes()).hexdigest(), passed=False,
              checks=checks)
began = time.monotonic()
def read(name, *ids):
    b = C.create_string_buffer(65536)
    n = getattr(api, 'trames_session_'+name)(h, *ids, b, len(b))
    assert n >= 0, (name, n)
    return b.raw[:n]
def send(t, text):
    b = text.encode()
    assert api.trames_session_terminal_input(h, t, b, len(b)) == len(b)
def pump(t):
    global status
    chunk = read('terminal_output', t)
    logs[t] = logs.get(t, b'') + chunk
    if chunk:
        with (a.output/f'terminal-{t}.log').open('ab') as f: f.write(chunk)
    wanted = logs[t].count(b'\x1b[6n')
    while queries.get(t, 0) < wanted:
        send(t, '\x1b[1;1R'); queries[t] = queries.get(t, 0)+1
    status = json.loads(read('status'))
    assert status['phase'] in ('starting', 'running'), status
def wait(t, needle, offset=0, timeout=90):
    end = time.monotonic()+timeout
    while True:
        pump(t)
        if needle in logs[t][offset:]: return
        assert time.monotonic() < end, ('timeout', needle, status, logs[t][-500:])
        time.sleep(.01)
def settle(t, seconds):
    end = time.monotonic()+seconds
    while time.monotonic() < end:
        pump(t); time.sleep(.01)
try:
    for terminal in (1, 2):
        if terminal == 2:
            assert api.trames_session_terminal_open(h) == 2
        wait(terminal, b'# ')
        offset = len(logs[terminal]); send(terminal, 'node\r'); wait(terminal, b'> ', offset)
        for text, expected, delayed in [('111', '111', False), ('111', '111', True),
                                        ('1 + 2', '3', True), ('({x: 42})', 'x: 42', True)]:
            offset = len(logs[terminal])
            if delayed:
                for char in text:
                    send(terminal, char); settle(terminal, .35)
                settle(terminal, 1)  # Exercise inspector-backed eager preview before Enter.
            else:
                send(terminal, text)
            send(terminal, '\r'); wait(terminal, b'> ', offset)
            output = logs[terminal][offset:]
            # Require result on a new line; matching the input echo is insufficient.
            plain = re.sub(rb'\x1b\[[0-9;]*[A-Za-z]', b'', output).replace(b'\r', b'')
            assert any(expected.encode() in line for line in plain.split(b'\n')[1:-1]), plain
            checks.append(dict(terminal=terminal, input=text, delayed=delayed, passed=True))
            print(f'PASS terminal={terminal} input={text!r} delayed={delayed}', flush=True)
        offset = len(logs[terminal]); send(terminal, '.exit\r'); wait(terminal, b'# ', offset)
        offset = len(logs[terminal])
        send(terminal, "printf '\\nNODE_REPL_RETURNED\\n'\r")
        wait(terminal, b'\r\nNODE_REPL_RETURNED\r\n', offset)
    report['passed'] = True
except BaseException as error:
    report['error'] = repr(error)
    raise
finally:
    report.update(seconds=time.monotonic()-began, status=status)
    (a.output/'results.json').write_text(json.dumps(report, indent=2)+'\n')
    api.trames_session_close(h)
assert report['passed']
