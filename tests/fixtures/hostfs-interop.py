#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Guest half of the native host-directory event/lock interoperability test."""
import ctypes
import fcntl
import json
import os
from pathlib import Path
import select
import struct
import sys
import threading
import time

root = Path('/work')
mode = sys.argv[1]
child = 0
if len(sys.argv) > 2 and sys.argv[2] == 'busy':
    child = os.fork()
    if child == 0:
        until = time.monotonic() + 0.3
        while time.monotonic() < until:
            pass
        os._exit(0)

if mode.startswith('notify-'):
    libc = ctypes.CDLL(None, use_errno=True)
    fd = libc.inotify_init1(0)
    assert fd >= 0, ctypes.get_errno()
    wd = libc.inotify_add_watch(fd, b'/work/watch', 0x100)
    assert wd >= 0, ctypes.get_errno()
    waiter = None
    if mode == 'notify-poll':
        waiter = select.poll()
        waiter.register(fd, select.POLLIN)
    elif mode == 'notify-epoll':
        waiter = select.epoll()
        waiter.register(fd, select.EPOLLIN)
    (root / 'ready').write_text('ready')
    if mode == 'notify-close-reuse':
        def close_original():
            time.sleep(0.05)
            os.close(fd)
            assert os.open(root / 'lock', os.O_RDONLY) == fd
            (root / 'closed').write_text('closed and reused')
        closer = threading.Thread(target=close_original)
        closer.start()
    if waiter:
        assert waiter.poll(2 if mode == 'notify-epoll' else 2000)
    data = os.read(fd, 65536)
    if mode == 'notify-close-reuse':
        closer.join()
    events = []
    offset = 0
    while offset < len(data):
        watched, mask, cookie, length = struct.unpack_from('iIII', data, offset)
        name = data[offset + 16:offset + 16 + length].split(b'\0')[0].decode()
        events.append(dict(wd=watched, mask=mask, cookie=cookie, name=name))
        offset += 16 + length
    assert any(e['mask'] & 0x100 and e['name'] == 'native-created' for e in events), events
    os.close(fd)
    result = dict(events=events)
elif mode == 'flock-wait':
    fd = os.open(root / 'lock', os.O_RDWR)
    (root / 'ready').write_text('ready')
    fcntl.flock(fd, fcntl.LOCK_EX)
    os.close(fd)
    result = dict(acquired=True)
elif mode == 'flock-hold':
    fd = os.open(root / 'lock', os.O_RDWR)
    fcntl.flock(fd, fcntl.LOCK_EX)
    duplicate = os.dup(fd)
    os.close(fd)
    (root / 'ready').write_text('ready')
    time.sleep(0.3)
    os.close(duplicate)
    result = dict(released=True)
else:
    raise AssertionError(mode)
(root / 'result').write_text(json.dumps(result))
if child:
    assert os.waitpid(child, 0)[1] == 0
print('INTEROP_OK:' + mode, flush=True)
