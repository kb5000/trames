# SPDX-License-Identifier: GPL-3.0-or-later
import asyncio
import concurrent.futures
import os
from pathlib import Path
import selectors
import socket
import sqlite3
import subprocess
import tempfile
import threading
import venv

with selectors.DefaultSelector() as selector:
    a, b = socket.socketpair()
    selector.register(a, selectors.EVENT_READ)
    assert b.send(b'ipc') == 3
    assert selector.select(timeout=1)[0][0].fileobj is a
    assert a.recv(3) == b'ipc'
    a.close()
    b.close()

async def task():
    await asyncio.sleep(0.002)
    result = await asyncio.create_subprocess_exec('/bin/sh', '-c', 'printf async', stdout=asyncio.subprocess.PIPE)
    out, err = await result.communicate()
    assert result.returncode == 0 and out == b'async' and err is None
    assert await asyncio.to_thread(lambda: 6 * 7) == 42

asyncio.run(task())
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
    assert list(pool.map(lambda n: n * n, range(6))) == [0, 1, 4, 9, 16, 25]
with tempfile.TemporaryDirectory() as directory:
    database = sqlite3.connect(str(Path(directory) / 'persistent.sqlite'))
    database.execute('create table values_test(value integer)')
    database.execute('insert into values_test values (42)')
    database.commit()
    database.close()
    database = sqlite3.connect(str(Path(directory) / 'persistent.sqlite'))
    assert database.execute('select value from values_test').fetchone() == (42,)
    database.close()
venv.EnvBuilder(with_pip=False).create('/root/m6/venv')
out = subprocess.check_output(['/root/m6/venv/bin/python3', '-c', 'import sys; print(sys.prefix)'])
assert out == b'/root/m6/venv\n'
print('python selectors/asyncio/subprocess/threads/sqlite/venv: ok')
