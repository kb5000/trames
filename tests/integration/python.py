#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Signed Alpine Python install, pycache repair and execution with 128 host FDs.

The HTTP server runs in the parent; only the guest process receives the low
RLIMIT_NOFILE. Existing APK signatures and the rootfs trust store are retained.
"""
import http.server
import os
from pathlib import Path
import re
import resource
import shutil
import subprocess
import tarfile
import tempfile
import threading
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
RUNNER = Path(os.getenv('TRAMES_RUNNER', str(ROOT / 'target/release/trames-run')))
CACHE = Path(os.getenv('TRAMES_ALPINE_CACHE', str(ROOT / 'target/alpine-download')))

def low_descriptors():
    resource.setrlimit(resource.RLIMIT_NOFILE, (128, 128))

def run(command):
    p = subprocess.run(list(map(str, command)), capture_output=True, timeout=600,
                       preexec_fn=low_descriptors)
    assert p.returncode == 0, (p.returncode, p.stdout[-3500:], p.stderr[-3500:])
    return p.stdout, p.stderr

def main():
    index = CACHE / 'APKINDEX.tar.gz'
    with tarfile.open(index) as archive:
        text = archive.extractfile('APKINDEX').read().decode()
    packages, providers = {}, {}
    for block in text.split('\n\n'):
        fields = {line[0]:line[2:] for line in block.splitlines() if len(line)>2 and line[1]==':'}
        if 'P' not in fields: continue
        packages[fields['P']] = fields
        for name in fields.get('p','').split():
            providers.setdefault(re.split('[<>=~]', name)[0], fields['P'])
    names = set()
    def include(name):
        if name in names: return
        names.add(name)
        for dependency in packages[name].get('D','').split():
            if dependency.startswith('!'): continue
            dependency = re.split('[<>=~]', dependency)[0]
            include(dependency if dependency in packages else providers[dependency])
    for name in ('python3', 'python3-pyc', 'python3-pycache-pyc0', 'pyc'): include(name)
    with tempfile.TemporaryDirectory(prefix='trames-python-') as directory:
        tmp = Path(directory)
        repo = tmp / 'repo/aarch64'; repo.mkdir(parents=True)
        shutil.copyfile(index, repo/index.name)
        for name in sorted(names):
            filename = name+'-'+packages[name]['V']+'.apk'
            cached = CACHE/filename
            if not cached.exists():
                with urllib.request.urlopen('https://dl-cdn.alpinelinux.org/alpine/v3.22/main/aarch64/'+filename,timeout=120) as response:
                    cached.write_bytes(response.read())
            shutil.copyfile(cached, repo/filename)
        class Handler(http.server.SimpleHTTPRequestHandler):
            def __init__(self,*a,**k): super().__init__(*a,directory=str(repo.parent),**k)
            def log_message(self,*a): pass
        server = http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
        worker = threading.Thread(target=server.serve_forever,daemon=True); worker.start()
        try:
            config = tmp/'repositories'
            config.write_text('http://198.18.0.1:'+str(server.server_port)+'\n')
            base = [RUNNER,'--storage',tmp/'store','--virtual-network','2','--host-map','198.18.0.1=127.0.0.1','--max-instructions','4000000000']
            if os.getenv("TRAMES_TEST_MUSL"):
                base += ["--musl-libc", os.environ["TRAMES_TEST_MUSL"], "--accelerator", "pure"]
            apk = ['--guest','/sbin/apk','--repositories-file','/test-repositories','--no-progress']
            out,err = run([*base,'--rootfs',CACHE/'alpine-minirootfs-3.22.6-aarch64.tar.gz','--file','/test-repositories='+str(config),*apk,'add','python3'])
            assert b'OK:' in out and b'ERROR:' not in out+err,(out[-3500:],err[-3500:])
            count = len(list((tmp/'store/objects').iterdir()))
            assert count > 1000,count
            print(f'Fresh signed apk add python3: {count} objects, host FD limit 128, no creation or permission errors',flush=True)
            # Repair the package containing the owner's failing pty.pyc, without
            # deleting the store or its unrelated files.
            run([*base,'--guest','/bin/sh','-c','printf retained >/root/owner-data'])
            out,err = run([*base,*apk,'fix','python3','python3-pycache-pyc0'])
            assert b'OK:' in out and b'ERROR:' not in out+err,(out[-3500:],err[-3500:])
            code = '''import math, json, sqlite3, pty, pathlib
assert math.sqrt(81) == 9
assert sqlite3.connect(":memory:").execute("select 42").fetchone() == (42,)
assert pathlib.Path("/usr/lib/python3.12/__pycache__/pty.cpython-312.pyc").is_file()
assert pathlib.Path("/root/owner-data").read_text() == "retained"
print(json.dumps({"python": "ok", "data": "retained"}))'''
            out,err = run([*base,'--guest','/usr/bin/python3','-c',code])
            assert out==b'{"python": "ok", "data": "retained"}\n' and not err,(out,err)
            print('Reopened store, signed apk fix, actual Python math/json/sqlite3/pty and retained owner data: passed',flush=True)
        finally:
            server.shutdown(); worker.join(); server.server_close()

if __name__ == '__main__': main()
