#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Alternating fixed syscall work in isolated processes using the App session ABI."""
import argparse
import ctypes as C
import hashlib
import json
import os
import re
from pathlib import Path
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline-ffi', type=Path)
p.add_argument('--candidate-ffi', type=Path, default=ROOT/'target/perf-runtime/release/libtrames_ffi.so')
p.add_argument('--fixture', type=Path, default=ROOT/'target/filtered-clocks/filtered-clocks')
p.add_argument('--rootfs', type=Path, default=ROOT/'target/alpine-download/alpine-minirootfs-3.22.6-aarch64.tar.gz')
p.add_argument('--output', type=Path, required=True)
p.add_argument('--samples', type=int, default=2)
p.add_argument('--iterations', type=int, default=2000)
p.add_argument('--probe-ffi', type=Path, help=argparse.SUPPRESS)
a = p.parse_args()
assert a.samples > 0 and 0 < a.iterations <= 100000
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()

if a.probe_ffi:
    # Each library owns native signal/worker state. Never load both into one
    # host process; benchmark one VM at a time with the same four-CPU affinity.
    os.sched_setaffinity(0, sorted(os.sched_getaffinity(0))[:4])
    api = C.CDLL(str(a.probe_ffi.resolve()))
    api.trames_session_open.argtypes = [C.c_char_p, C.c_uint32]
    api.trames_session_open.restype = C.c_uint64
    for name in ('input', 'transfer'):
        getattr(api, 'trames_session_'+name).argtypes = [C.c_uint64, C.c_char_p, C.c_uint32]
    for name in ('status', 'output'):
        getattr(api, 'trames_session_'+name).argtypes = [C.c_uint64, C.c_void_p, C.c_uint32]
    api.trames_session_close.argtypes = [C.c_uint64]
    a.output.mkdir(parents=True, exist_ok=False)
    options = json.dumps(dict(store=str((a.output/'store').resolve()), rootfs=str(a.rootfs.resolve()),
        volume_total=16<<30, volume_available=16<<30, network='off',
        memory_budget_bytes=1<<30, process_memory_budget_bytes=512<<20)).encode()
    h = api.trames_session_open(options, len(options))
    if not h:
        api.trames_last_error.argtypes = [C.c_void_p, C.c_uint32]
        message = C.create_string_buffer(4096)
        api.trames_last_error(message, len(message))
        raise RuntimeError(message.value.decode(errors='replace'))
    data, queries = bytearray(), 0
    def send(text):
        b = text.encode(); assert api.trames_session_input(h, b, len(b)) == len(b)
    def read(name):
        b = C.create_string_buffer(65536)
        n = getattr(api, 'trames_session_'+name)(h, b, len(b)); assert n >= 0
        return b.raw[:n]
    def pump():
        global queries
        data.extend(read('output'))
        wanted = data.count(b'\x1b[6n')
        while queries < wanted:
            send('\x1b[1;5R'); queries += 1
        status = json.loads(read('status'))
        assert status['phase'] in ('running', 'starting'), status
    def settle_prompt():
        # ash asks for a cursor report after printing the prompt. Let that
        # exchange finish before submitting another command; it otherwise may
        # consume command bytes as a malformed report. This is outside timing.
        for _ in range(25):
            pump(); time.sleep(.01)
    def wait(marker, timeout=90):
        began = time.monotonic()
        while marker not in data:
            pump()
            failed = re.search(rb'\r\nFILTERED_(?:READY|DONE):([1-9][0-9]*)\r\n', data)
            assert not failed, data[-4096:]
            assert time.monotonic()-began < timeout, data[-4096:]
            time.sleep(.001)
    try:
        began = time.monotonic()
        while json.loads(read('status'))['phase'] == 'starting':
            assert time.monotonic()-began < 60
            time.sleep(.005)
        transfer = json.dumps(dict(guest='/tmp/filtered-clocks', host=str(a.fixture.resolve()),
                                   export=False, replace=True)).encode()
        assert api.trames_session_transfer(h, transfer, len(transfer)) >= 0
        settle_prompt()
        send("chmod 755 /tmp/filtered-clocks; printf '\\nFILTERED_READY:%d\\n' $?\n")
        wait(b'\r\nFILTERED_READY:0\r\n')
        settle_prompt()
        began = time.monotonic()
        send(f"/tmp/filtered-clocks {a.iterations} 4; printf '\\nFILTERED_DONE:%d\\n' $?\n")
        wait(b'\r\nFILTERED_DONE:0\r\n')
        elapsed = time.monotonic()-began
        assert b'filtered-clocks: denied call, monotonic clocks and identities: ok' in data
        (a.output/'result.json').write_text(json.dumps(dict(seconds=elapsed, ffi_sha256=digest(a.probe_ffi)))+'\n')
    finally:
        api.trames_session_close(h)
    sys.exit(0)

assert a.baseline_ffi, '--baseline-ffi is required'
a.output.mkdir(parents=True, exist_ok=False)
rows = []
for sample in range(a.samples):
    versions = [('before', a.baseline_ffi), ('after', a.candidate_ffi)]
    if sample % 2: versions.reverse()
    for version, ffi in versions:
        output = a.output/f'{sample}-{version}'
        subprocess.run([sys.executable, str(Path(__file__).resolve()), '--probe-ffi', str(ffi.resolve()),
            '--fixture', str(a.fixture.resolve()), '--rootfs', str(a.rootfs.resolve()),
            '--iterations', str(a.iterations), '--output', str(output.resolve())], check=True, timeout=120)
        row = json.loads((output/'result.json').read_text())
        rows.append(dict(sample=sample, version=version, **row))
        print(version, sample, round(row['seconds'], 6), flush=True)
medians = {version: statistics.median(r['seconds'] for r in rows if r['version']==version)
           for version in ('before','after')}
report = dict(rows=rows, medians=medians, speedup=medians['before']/medians['after'],
    fixture_sha256=digest(a.fixture), rootfs_sha256=digest(a.rootfs), iterations=a.iterations,
    threads=4, limits='Fixed filtered clock/getpid work through the App ABI; includes shell fork/exit and terminal polling, excludes rootfs/import. Two alternating samples by default. Not Firefox, CoreMark or an iPad speedup. Run without other live guests.')
(a.output/'results.json').write_text(json.dumps(report, indent=2)+'\n')
print('median speedup', round(report['speedup'], 3))
