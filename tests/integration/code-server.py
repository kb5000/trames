#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Manual heavy probe of a real ARM64/musl code-server in a prepared guest store.

See docs/guides/code-server.md for inputs. No host Node backend or dummy editor
is substituted. The optional browser runs natively against the guest listener.
"""
import argparse
import http.client
import json
import os
from pathlib import Path
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def import_package(base, source):
    source = source.resolve()
    files = sorted(p for p in source.rglob('*') if p.is_file() and not p.is_symlink()
                   and '/build/' not in str(p.relative_to(source)) and not p.name.endswith('.original'))
    files += sorted(source.glob('lib/vscode/node_modules/*/build/Release/*.node'))
    files += sorted(source.glob('lib/vscode/node_modules/@*/*/build/Release/*.node'))
    for start in range(0, len(files), 200):
        args = []
        for path in files[start:start + 200]:
            flag = '--exec-file' if path.stat().st_mode & 0o111 else '--file'
            args += [flag, '/opt/code-server/' + str(path.relative_to(source)) + '=' + str(path)]
        subprocess.run(base + args + ['--guest', '/bin/true'], check=True, timeout=180)
        print('Imported', min(start + 200, len(files)), '/', len(files), flush=True)
    import shlex
    commands = []
    for path in source.rglob('*'):
        if path.is_symlink():
            guest = '/opt/code-server/' + str(path.relative_to(source))
            commands += ['mkdir -p ' + shlex.quote(str(Path(guest).parent)),
                         'ln -s ' + shlex.quote(os.readlink(path)) + ' ' + shlex.quote(guest)]
    commands += ['cd /opt/code-server/lib/vscode',
                 'test -e bin/remote-cli/code-server || ln -s code-linux.sh bin/remote-cli/code-server',
                 'test -e bin/helpers/browser.sh || ln -s browser-linux.sh bin/helpers/browser.sh']
    subprocess.run(base + ['--guest', '/bin/sh', '-ec', '; '.join(commands)], check=True, timeout=180)


def probe_browser(url, output):
    from playwright.sync_api import sync_playwright
    result = {'websockets': [], 'errors': [], 'console': []}
    began = time.monotonic()
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(headless=True, args=['--no-sandbox'])
        try:
            page = browser.new_page(viewport={'width': 1280, 'height': 900})
            page.on('websocket', lambda ws: result['websockets'].append(ws.url))
            page.on('pageerror', lambda error: result['errors'].append(str(error)))
            page.on('console', lambda msg: result['console'].append({'type': msg.type, 'text': msg.text}))
            response = page.goto(url + '/?folder=/root/project', timeout=600000, wait_until='domcontentloaded')
            result['http_status'] = response.status
            page.wait_for_selector('.monaco-workbench', timeout=600000)
            page.wait_for_timeout(30000)
            result['title'] = page.title()
            result['body'] = page.locator('body').inner_text()
            result['seconds'] = time.monotonic() - began
            result['workbench_shell'] = True
            result['editing_verified'] = False
            result['terminal_verified'] = False
            try:
                page.screenshot(path=str(output / 'workbench.png'), timeout=5000)
            except Exception as error:
                result['screenshot_error'] = str(error)
            assert result['websockets'], 'workbench did not attempt a WebSocket connection'
            # DOM creation and a WebSocket attempt are diagnostics, not proof of
            # editing/terminal acceptance. Keep those separate in the report.
            return result
        finally:
            (output / 'browser.json').write_text(json.dumps(result, indent=2) + '\n')
            browser.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--store', type=Path, required=True, help='prepared, disposable Node 24 guest store')
    parser.add_argument('--package', type=Path, help='import a prepared upstream npm tree into a fresh /opt/code-server')
    parser.add_argument('--runner', default=os.getenv('TRAMES_RUNNER', str(ROOT / 'target/release/trames-run')))
    parser.add_argument('--musl', type=Path, default=ROOT / 'target/musl-hybrid/libc.so')
    parser.add_argument('--output', type=Path, default=ROOT / 'target/code-server-probe')
    parser.add_argument('--browser', action='store_true', help='also run the native Playwright browser probe')
    parser.add_argument('--version-only', action='store_true')
    parser.add_argument('--timeout', type=int, default=1800)
    parser.add_argument('--cpus', type=int, default=4)
    parser.add_argument('--affinity', default='', help='guest taskset CPU list; empty to test unrestricted execution')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    base = [args.runner, '--storage', str(args.store.resolve()), '--memory-budget', '3GiB',
            '--process-memory-budget', '1GiB', '--cpus', str(args.cpus),
            '--musl-libc', str(args.musl.resolve()), '--accelerator', 'pure',
            '--max-instructions', '1000000000000']
    if os.environ.get('CODE_SERVER_HANDSHAKE_TIMEOUT_MS'):
        base += ['--env', 'CODE_SERVER_HANDSHAKE_TIMEOUT_MS=' + os.environ['CODE_SERVER_HANDSHAKE_TIMEOUT_MS']]
    if args.package:
        import_package(base, args.package)
    entry = ['--guest'] + (['/usr/bin/taskset', '-c', args.affinity] if args.affinity else []) + ['/usr/bin/node', '/opt/code-server/out/node/entry.js']
    began = time.monotonic()
    with (args.output / 'version.log').open('wb') as log:
        version = subprocess.run(base + ['--metrics'] + entry + ['--version'], stdout=log,
                                 stderr=subprocess.STDOUT, timeout=args.timeout)
    result = {'version_exit': version.returncode, 'version_seconds': time.monotonic() - began}
    (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    assert version.returncode == 0, 'guest version probe failed; see version.log'
    print('Guest version probe:', result, flush=True)
    if args.version_only:
        return
    subprocess.run(base + ['--guest', '/bin/sh', '-ec',
                           "mkdir -p /root/project; printf 'hello Trames code-server\\n' >/root/project/hello.txt"],
                   check=True, timeout=120)
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        port = listener.getsockname()[1]
    url = f'http://127.0.0.1:{port}'
    # Authentication is disabled only for this loopback-only disposable test.
    command = base + ['--metrics'] + entry + ['--auth', 'none', '--bind-addr', f'127.0.0.1:{port}',
        '--user-data-dir', '/root/code-data', '--extensions-dir', '/root/code-ext',
        '--disable-telemetry', '--disable-update-check', '--log', 'debug', '/root/project']
    with (args.output / 'server.log').open('wb') as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        began = time.monotonic()
        try:
            while time.monotonic() - began < args.timeout:
                assert process.poll() is None, f'guest server exited {process.returncode}; see server.log'
                try:
                    connection = http.client.HTTPConnection('127.0.0.1', port, timeout=1)
                    connection.request('GET', '/healthz')
                    response = connection.getresponse()
                    body = response.read()
                    connection.close()
                    if response.status == 200:
                        result.update({'http_seconds': time.monotonic() - began,
                                       'healthz': json.loads(body), 'url': url})
                        print('Guest HTTP ready:', result, flush=True)
                        if args.browser:
                            result['browser'] = probe_browser(url, args.output)
                        break
                except (OSError, TimeoutError):
                    pass
                time.sleep(1)
            else:
                raise TimeoutError('guest HTTP startup exceeded the probe timeout')
        finally:
            result['server_exit_before_cleanup'] = process.poll()
            (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == '__main__':
    main()
