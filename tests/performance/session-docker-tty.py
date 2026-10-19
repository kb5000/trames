#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Time cached-image Docker -it through the App's production terminal session.

Copies an installed Docker store; never invokes host Docker or pulls an image.
The guest command is docker run --rm -it IMAGE sh. Host waits for the real inner
shell prompt before sending a tty/stdio roundtrip and exit. Optional daemon debug
logging retains API/containerd timestamps; no runtime/security/CPU overrides.
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import platform
import os
import re
import shlex
import shutil
import tempfile
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--library', type=Path, required=True)
p.add_argument('--store', type=Path, required=True)
p.add_argument('--image', default='default-probe')
p.add_argument('--output', type=Path, required=True)
p.add_argument('--timeout', type=float, default=1200)
p.add_argument('--debug-daemon', action='store_true')
p.add_argument('--peer-probe', action='store_true', help='measure another real terminal during startup')
p.add_argument('--sampler', type=Path, help='optional Linux per-thread loaded-FFI PC sampler')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
assert a.timeout > 0
api = C.CDLL(str(a.library.resolve()))
api.trames_session_open.argtypes = [C.c_char_p, C.c_uint32]
api.trames_session_open.restype = C.c_uint64
api.trames_session_close.argtypes = [C.c_uint64]
api.trames_session_terminal_open.argtypes = [C.c_uint64]
api.trames_session_terminal_open.restype = C.c_uint64
api.trames_last_error.argtypes = [C.c_void_p, C.c_uint32]
api.trames_session_terminal_input.argtypes = [C.c_uint64, C.c_uint64, C.c_char_p, C.c_uint32]
api.trames_session_terminal_output.argtypes = [C.c_uint64, C.c_uint64, C.c_void_p, C.c_uint32]
api.trames_session_status.argtypes = [C.c_uint64, C.c_void_p, C.c_uint32]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def error():
    b = C.create_string_buffer(8192)
    n = api.trames_last_error(b, len(b))
    return b.raw[:max(0, n)].decode()


script = r'''set -eu
mark() { printf '\nTRAMES_TTY_%s\n' "$1"; }
daemon=
cleanup() {
  test -z "$daemon" || { kill "$daemon" 2>/dev/null || :; wait "$daemon" 2>/dev/null || :; }
  mark LOG_BEGIN
  cat /tmp/tty-dockerd.log
  dmesg
  mark LOG_END
}
trap cleanup EXIT
mark DAEMON_BEGIN
dockerd@DEBUG@ >/tmp/tty-dockerd.log 2>&1 &
daemon=$!
while ! grep -q 'API listen on' /tmp/tty-dockerd.log; do
  kill -0 "$daemon"
  sleep 1
done
mark DAEMON_READY
docker image inspect @IMAGE@ --format 'IMAGE={{.Id}}' || exit
printf 'GUEST_CPUS='; grep -c '^processor' /proc/cpuinfo
mark RUN_BEGIN
docker run --rm -it @IMAGE@ sh
mark RUN_END
mark COMPLETE
'''.replace('@DEBUG@', ' --debug' if a.debug_daemon else '').replace('@IMAGE@', shlex.quote(a.image))
(a.output / 'guest.sh').write_text(script)
report = dict(scope=__doc__, host=platform.machine(), library_sha256=sha(a.library),
              input_store_manifest_sha256=sha(a.store / 'manifest'), image=a.image,
              command='docker run --rm -it ' + shlex.quote(a.image) + ' sh',
              debug_daemon=a.debug_daemon, peer_probe=a.peer_probe, sampled=bool(a.sampler),
              markers={}, completed=False)
output = bytearray()
answered_cursor_queries = 0
began = time.monotonic()
handle = None
peer = None
peer_output = bytearray()
peer_queries = 0
peer_ready = False
peer_pending = None
peer_sequence = 0
peer_last = 0
peer_sent = None
peer_latencies = []
thread_samples = []
thread_last = 0
sampler = None
if a.sampler:
    sampler = C.CDLL(str(a.sampler.resolve()))
    sampler.trames_sample_start.argtypes = [C.c_char_p, C.c_char_p]
try:
    with tempfile.TemporaryDirectory(prefix='trames-session-docker-tty-') as tmp:
        store = Path(tmp) / 'store'
        shutil.copytree(a.store, store)
        options = dict(store=str(store), volume_total=16 << 30, volume_available=16 << 30,
                       network='full', wayland=True, multiple_terminals=True,
                       memory_budget_bytes=4 << 30, process_memory_budget_bytes=4 << 30)
        data = json.dumps(options).encode()
        handle = api.trames_session_open(data, len(data))
        assert handle, error()

        def send(s):
            b = s.encode()
            assert api.trames_session_terminal_input(handle, 1, b, len(b)) == len(b), error()

        def pump():
            global answered_cursor_queries, peer_queries, peer_pending, peer_last, peer_sequence, peer_sent, thread_last
            now = time.monotonic()
            if a.peer_probe and now - thread_last >= .2:
                rows = {}
                for task in Path('/proc/self/task').iterdir():
                    try:
                        fields = (task / 'stat').read_text().rsplit(')', 1)[1].split()
                        rows[task.name] = dict(name=(task / 'comm').read_text().strip(), ticks=int(fields[11]) + int(fields[12]))
                    except (FileNotFoundError, ProcessLookupError):
                        pass
                thread_samples.append(dict(seconds=now-began, threads=rows))
                thread_last = now
            if peer:
                b = C.create_string_buffer(65536)
                n = api.trames_session_terminal_output(handle, peer, b, len(b)); assert n >= 0, error()
                peer_output.extend(b.raw[:n])
                queries = peer_output.count(b'\x1b[6n')
                while peer_queries < queries:
                    raw = b'\x1b[1;5R'; assert api.trames_session_terminal_input(handle, peer, raw, len(raw)) == len(raw)
                    peer_queries += 1
                if peer_ready:
                    if peer_pending is not None and ('\r\nTRAMES_PEER_%d\r\n' % peer_pending).encode() in peer_output:
                        peer_latencies.append(dict(sequence=peer_pending, seconds=now-began, latency=now-peer_sent,
                            stage='run' if 'RUN_BEGIN' in report['markers'] and 'CONTAINER_PROMPT' not in report['markers'] else 'other'))
                        peer_pending = None
                    if peer_pending is None and now-peer_last >= .2:
                        peer_sequence += 1; peer_pending = peer_sequence
                        raw = ("printf '\\nTRAMES_PEER_%d\\n'\n" % peer_sequence).encode()
                        peer_sent = time.monotonic()
                        assert api.trames_session_terminal_input(handle, peer, raw, len(raw)) == len(raw), error()
                        peer_last = now

            b = C.create_string_buffer(65536)
            n = api.trames_session_terminal_output(handle, 1, b, len(b))
            assert n >= 0, error()
            output.extend(b.raw[:n])
            if n:
                (a.output / 'stdout.log').write_bytes(output)
            # BusyBox asks where its initial four-column root prompt ended.
            # SwiftTerm answers this in the App; a raw C ABI reader must too.
            queries = output.count(b'\x1b[6n')
            while answered_cursor_queries < queries:
                send('\x1b[1;5R')
                answered_cursor_queries += 1
            for marker in ('DAEMON_BEGIN', 'DAEMON_READY', 'RUN_BEGIN', 'RUN_END', 'COMPLETE', 'LOG_END'):
                if marker not in report['markers'] and ('\r\nTRAMES_TTY_' + marker + '\r\n').encode() in output:
                    report['markers'][marker] = time.monotonic() - began
                    print(marker, round(report['markers'][marker], 3), flush=True)

        def wait_for(predicate, seconds=None):
            deadline = min(began + a.timeout, time.monotonic() + (seconds or a.timeout))
            while time.monotonic() < deadline:
                pump()
                if predicate():
                    return
                b = C.create_string_buffer(65536)
                n = api.trames_session_status(handle, b, len(b))
                assert n >= 0, error()
                status = json.loads(b.raw[:n])
                assert status['phase'] not in ('failed', 'exited', 'cancelled'), status
                time.sleep(.01)
            raise TimeoutError(bytes(output[-2000:]))

        # Wait for and answer the complete query before queueing a command.
        # Seeing only the prompt prefix can race a query in the next output
        # packet, causing the query reader to consume command bytes as a reply.
        wait_for(lambda: b'# ' in output and answered_cursor_queries > 0, 20)
        # BusyBox's cursor-query read can prefetch a coalesced keyboard command
        # and discard that lookahead. Keep the reply and first command in
        # separate guest reads, as the native terminal and human input do.
        time.sleep(.1)
        send("stty -echo; printf '\\nTRAMES_TTY_HOST_READY\\n'\n")
        wait_for(lambda: b'\r\nTRAMES_TTY_HOST_READY\r\n' in output, 20)
        if a.peer_probe:
            peer = api.trames_session_terminal_open(handle); assert peer, error()
            wait_for(lambda: b'# ' in peer_output and peer_queries > 0, 20)
            time.sleep(.1)
            raw = b"stty -echo; printf '\\nTRAMES_PEER_READY\\n'\n"
            assert api.trames_session_terminal_input(handle, peer, raw, len(raw)) == len(raw)
            wait_for(lambda: b'\r\nTRAMES_PEER_READY\r\n' in peer_output, 20)
            peer_ready = True
        if sampler:
            (a.output / 'host.maps').write_text(Path('/proc/self/maps').read_text())
            report['sampler_begin'] = time.monotonic() - began
            assert sampler.trames_sample_start(str(a.library.resolve()).encode(), str((a.output / 'host.pcs').resolve()).encode()) == 0
        output.clear()
        answered_cursor_queries = 0
        send("cat >/tmp/tty-profile.sh <<'TRAMES_TTY_SCRIPT_END'\n" + script +
             "TRAMES_TTY_SCRIPT_END\n/bin/sh /tmp/tty-profile.sh\n")
        wait_for(lambda: 'RUN_BEGIN' in report['markers'] and answered_cursor_queries > 0 and
                 re.search(rb'(?:\r?\n)/ # $',
                           re.sub(rb'\x1b\[[0-?]*[ -/]*[@-~]', b'', output)) is not None)
        report['markers']['CONTAINER_PROMPT'] = time.monotonic() - began
        print('CONTAINER_PROMPT', round(report['markers']['CONTAINER_PROMPT'], 3), flush=True)
        time.sleep(.1)
        send("test -t 0 && test -t 1 && test -t 2 && printf '\\nTRAMES_TTY_ROUNDTRIP:%s\\n' \"$(tty)\"; exit\n")
        wait_for(lambda: 'LOG_END' in report['markers'])
        report['completed'] = (b'\r\nTRAMES_TTY_ROUNDTRIP:/dev/pts/' in output and
                               'COMPLETE' in report['markers'])
        assert report['completed'], bytes(output[-2000:])
except Exception as exc:
    report['error'] = repr(exc)
    raise
finally:
    if sampler:
        sampler.trames_sample_stop()
    if a.peer_probe:
        report['peer_latencies'] = peer_latencies
        report['host_clock_ticks'] = os.sysconf('SC_CLK_TCK')
        (a.output / 'threads.json').write_text(json.dumps(thread_samples) + '\n')
        (a.output / 'peer.log').write_bytes(peer_output)
    if handle:
        api.trames_session_close(handle)
    report['seconds'] = time.monotonic() - began
    m = report['markers']
    report['stages'] = {label: m[end] - m[start] for label, start, end in (
        ('daemon', 'DAEMON_BEGIN', 'DAEMON_READY'),
        ('run_to_prompt', 'RUN_BEGIN', 'CONTAINER_PROMPT'),
        ('roundtrip_exit_rm', 'CONTAINER_PROMPT', 'RUN_END')) if start in m and end in m}
    (a.output / 'stdout.log').write_bytes(output)
    (a.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2), flush=True)
