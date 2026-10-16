#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unmodified signed Alpine Node.js: default V8, libuv and persistent files."""
import http.server
import os
from pathlib import Path
import subprocess
import tempfile
import threading

from m6 import CACHE, RUNNER, ROOT, repository


def main():
    (ROOT / 'target/nodejs-workflow.log').write_text('')
    with tempfile.TemporaryDirectory(prefix='trames-nodejs-') as directory:
        tmp = Path(directory)
        repository(tmp / 'repo', tools=('nodejs', 'npm'))

        class Handler(http.server.SimpleHTTPRequestHandler):
            def __init__(self, *args, **kwargs):
                super().__init__(*args, directory=str(tmp / 'repo'), **kwargs)

            def log_message(self, *args):
                pass

        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            store = Path(os.getenv('TRAMES_NODEJS_STORE', tmp / 'store'))
            config = tmp / 'repositories'
            config.write_text(''.join(f'http://198.18.0.1:{server.server_port}/{name}\n'
                                      for name in ('main', 'community')))
            base = [str(RUNNER), '--storage', str(store), '--virtual-network', '2',
                    '--host-map', '198.18.0.1=127.0.0.1', '--max-instructions', '100000000000']
            if os.getenv('TRAMES_CPUS'):
                base += ['--cpus', os.environ['TRAMES_CPUS'], '--env', 'TRAMES_SMP_WORKERS=1']
            if os.getenv('TRAMES_TEST_MUSL'):
                base += ['--musl-libc', os.environ['TRAMES_TEST_MUSL'], '--accelerator', 'pure']
            if os.getenv('TRAMES_NATIVE_ALLOCATOR')=='1':
                base += ['--native-allocator']

            def run(args, timeout=900):
                # Keep guest stage markers visible while a later operation is
                # pending, and retain the transcript as a CI artifact.
                transcript = ROOT / 'target/nodejs-workflow.log'
                with transcript.open('ab') as output:
                    offset = output.tell()
                    result = subprocess.run(base + list(map(str, args)), stdout=output,
                                            stderr=subprocess.PIPE, timeout=timeout)
                result.stdout = transcript.read_bytes()[offset:]
                assert result.returncode == 0, (args, result.returncode,
                                                result.stdout[-4000:], result.stderr[-4000:])
                return result.stdout, result.stderr

            initial = [] if (store / 'manifest').exists() else [
                '--rootfs', CACHE / 'alpine-minirootfs-3.22.6-aarch64.tar.gz']
            config_path = '/node-repositories-' + tmp.name
            out, err = run([*initial, '--file', config_path + '=' + str(config),
                            '--guest', '/sbin/apk', '--repositories-file', config_path,
                            '--no-progress', 'add', 'nodejs', 'npm'])
            assert b'OK:' in out and b'ERROR:' not in out + err, (out, err)
            print('Signed apk add nodejs npm: passed', flush=True)
            out, err = run(['--guest', '/usr/bin/node', '--version'])
            assert out.startswith(b'v22.') and not err, (out, err)
            print('Default Node.js:', out.decode().strip(), flush=True)
            fixture_path = '/root/node-compat-' + tmp.name + '.cjs'
            out, err = run(['--file', fixture_path + '=' + str(ROOT / 'tests/fixtures/node-compat.cjs'),
                            '--guest', '/usr/bin/node', fixture_path,
                            f'http://198.18.0.1:{server.server_port}/main/aarch64/APKINDEX.tar.gz'])
            assert out.endswith(b'node compatibility: ok\n') and not err, (out, err)
            print(out.decode(), end='', flush=True)
            watch_path = '/root/node-watch-' + tmp.name + '.cjs'
            out, err = run(['--file', watch_path + '=' + str(ROOT / 'tests/fixtures/node-inotify.cjs'),
                            '--guest', '/usr/bin/node', watch_path])
            assert out == b'node-inotify-ok\n' and not err, (out, err)
            print('Real Node.js fs.watch / libuv inode notifications: passed', flush=True)
            handoff_path = '/root/node-handoff-' + tmp.name + '.cjs'
            out, err = run(['--file', handoff_path + '=' + str(ROOT / 'tests/fixtures/node-socket-handoff.cjs'),
                            '--guest', '/usr/bin/node', handoff_path])
            assert out == b'node-socket-handoff-ok\n' and not err, (out, err)
            print('Real Node.js fork IPC and accepted TCP socket handoff: passed', flush=True)
            out, err = run(['--guest', '/bin/sh', '-ec', 'npm --version'])
            assert out.startswith(b'11.') and not err, (out, err)
            run(['--guest', '/bin/sh', '-c', 'mkdir -p /root/node-offline/dep'])
            app = tmp / 'package.json'
            app.write_text('{"name":"trames-node-test","version":"1.0.0","private":true}')
            dep = tmp / 'dep.json'
            dep.write_text('{"name":"offline-probe","version":"1.0.0","main":"index.cjs"}')
            source = tmp / 'index.cjs'
            source.write_text('module.exports = 42;\n')
            # Imports create new files; remove only this fixture's prior inputs.
            run(['--guest', '/bin/sh', '-c', 'rm -f /root/node-offline/package.json /root/node-offline/dep/package.json /root/node-offline/dep/index.cjs'])
            out, err = run(['--file', '/root/node-offline/package.json=' + str(app),
                            '--file', '/root/node-offline/dep/package.json=' + str(dep),
                            '--file', '/root/node-offline/dep/index.cjs=' + str(source),
                            '--guest', '/bin/sh', '-ec',
                            'cd /root/node-offline; npm install ./dep --offline --ignore-scripts --no-audit --no-fund; '
                            'node -e "require(\'node:assert/strict\').equal(require(\'offline-probe\'), 42); '
                            'require(\'node:fs\').accessSync(\'package-lock.json\'); console.log(\'npm offline: ok\')"'])
            assert out.endswith(b'npm offline: ok\n') and b'ERR!' not in err, (out, err)
            print('npm offline local dependency, lockfile and CommonJS import: passed', flush=True)
            # A distinct runtime session reopens and checks both app and npm data.
            out, err = run(['--guest', '/usr/bin/node', '-e',
                            'const assert=require("node:assert/strict"); '
                            'assert.equal(require("node:fs").readFileSync("/root/node-retained.txt","utf8"),"retained Node.js data\\n"); '
                            'assert.equal(require("/root/node-offline/node_modules/offline-probe"),42); console.log("node reopen: ok")'])
            assert out == b'node reopen: ok\n' and not err, (out, err)
            print('Reopened store: Node.js and npm owner data retained', flush=True)
        finally:
            server.shutdown()
            worker.join()
            server.server_close()


if __name__ == '__main__':
    main()
