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
    def runtime_file(path):
        if not path.is_file() or path.is_symlink() or path.name.endswith('.original'):
            return False
        parts = path.relative_to(source).parts
        if 'build' not in parts:
            return True
        # Several npm packages SHIP JavaScript under build/ (socks,
        # smart-buffer, ...). Exclude compiler objects, not that directory.
        return (path.suffix not in ('.o', '.a', '.d', '.obj', '.lib', '.pdb', '.ipdb', '.iobj', '.tlog')
                and not any(p in parts for p in ('obj.target', '.deps', 'node_gyp_bins'))
                and not ('Release' in parts and 'obj' in parts))
    files = sorted(p for p in source.rglob('*') if runtime_file(p))
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
    result = {'websockets': [], 'errors': [], 'console': [], 'requests': []}
    began = time.monotonic()
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(headless=True, args=['--no-sandbox'], handle_sigint=False)
        try:
            headers = {'Accept-Encoding': 'identity'} if os.getenv('TRAMES_CODE_SERVER_IDENTITY') == '1' else {}
            result['request_headers'] = headers
            page = browser.new_page(viewport={'width': 1280, 'height': 900}, extra_http_headers=headers)
            timeout = int(os.getenv('CODE_SERVER_HANDSHAKE_TIMEOUT_MS', '10000'))
            assert 10000 <= timeout <= 600000
            page.add_init_script(f'globalThis.TRAMES_REMOTE_HANDSHAKE_TIMEOUT_MS={timeout};')
            def websocket(ws):
                entry = {'url': ws.url, 'sent': 0, 'received': 0, 'closed': False}
                result['websockets'].append(entry)
                ws.on('framesent', lambda _: entry.update(sent=entry['sent'] + 1))
                ws.on('framereceived', lambda _: entry.update(received=entry['received'] + 1))
                ws.on('close', lambda: entry.update(closed=True))
            page.on('websocket', websocket)
            def request_finished(request):
                result['requests'].append({'url': request.url, 'timing': request.timing})
                (output / 'browser-progress.json').write_text(json.dumps(result, indent=2) + '\n')
            page.on('requestfinished', request_finished)
            page.on('requestfailed', lambda request: result['errors'].append(request.url + ': ' + str(request.failure)))
            page.on('pageerror', lambda error: result['errors'].append(str(error)))
            def console(msg):
                result['console'].append({'type': msg.type, 'text': msg.text})
                (output / 'browser-progress.json').write_text(json.dumps(result, indent=2) + '\n')
            page.on('console', console)
            response = page.goto(url + '/?folder=/root/project', timeout=600000, wait_until='domcontentloaded')
            result['http_status'] = response.status
            page.wait_for_selector('.monaco-workbench', timeout=600000)
            page.wait_for_timeout(30000)
            try:
                page.keyboard.press('Control+p')
                page.locator('.quick-input-widget input').fill('/root/project/hello.txt', timeout=300000)
                page.locator('.quick-input-list .monaco-list-row').filter(has_text='hello.txt').first.wait_for(state='visible', timeout=300000)
                page.keyboard.press('Enter')
                page.wait_for_function("Array.from(document.querySelectorAll('.part.editor .monaco-editor .view-lines')).some(e => e.innerText.includes('hello Trames code-server'))", timeout=300000)
                result['opened_file'] = True
                page.locator('.part.editor .monaco-editor textarea.inputarea').first.focus()
                page.keyboard.press('Control+End')
                page.keyboard.insert_text('edited through guest code-server\n')
                page.keyboard.press('Control+s')
                result['save_requested'] = True
                page.wait_for_timeout(10000)
                page.keyboard.press('Control+w')
                page.keyboard.press('Control+p')
                page.locator('.quick-input-widget input').fill('/root/project/hello.txt')
                page.locator('.quick-input-list .monaco-list-row').filter(has_text='hello.txt').first.wait_for(state='visible', timeout=300000)
                page.keyboard.press('Enter')
                page.wait_for_function("Array.from(document.querySelectorAll('.part.editor .monaco-editor .view-lines')).some(e => e.innerText.includes('edited through guest code-server'))", timeout=300000)
                result['reopened_file'] = True
            except Exception as error:
                result['file_error'] = str(error)
            try:
                page.keyboard.press('Escape')
                page.keyboard.press('Control+`')
                page.locator('.xterm-helper-textarea').first.wait_for(state='attached', timeout=300000)
                page.locator('.xterm-helper-textarea').first.focus()
                page.keyboard.type("printf 'TRAMES_TERMINAL_OK\\n' > /root/project/terminal-result.txt", delay=20)
                page.keyboard.press('Enter')
                result['terminal_command_sent'] = True
                page.wait_for_timeout(60000)
            except Exception as error:
                result['terminal_error'] = str(error)
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
    parser.add_argument('--lean-workbench', action='store_true', help='test settings: disable AI, background updates and shell integration')
    parser.add_argument('--browser', action='store_true', help='also run the native Playwright browser probe')
    parser.add_argument('--version-only', action='store_true')
    parser.add_argument('--skip-version', action='store_true', help='reuse an already validated package version')
    parser.add_argument('--serve-only', action='store_true', help='keep guest server alive for separately attached browser probes')
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
    result = {'version_skipped': True}
    if not args.skip_version:
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
                           "mkdir -p /root/project; printf 'hello Trames code-server\\n' >/root/project/hello.txt; rm -f /root/project/terminal-result.txt"],
                   check=True, timeout=120)
    if args.lean_workbench:
        subprocess.run(base + ['--file', '/tmp/trames-code-settings.json=' + str(ROOT / 'tests/fixtures/code-server-settings.json'),
                              '--guest', '/bin/sh', '-ec', 'mkdir -p /root/code-data/User /root/code-data/Machine; mv /tmp/trames-code-settings.json /root/code-data/User/settings.json; cp /root/code-data/User/settings.json /root/code-data/Machine/settings.json'],
                       check=True, timeout=120)
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        port = listener.getsockname()[1]
    url = f'http://127.0.0.1:{port}'
    # Authentication is disabled only for this loopback-only disposable test.
    command = base + ['--metrics'] + entry + ['--auth', 'none', '--bind-addr', f'127.0.0.1:{port}',
        '--user-data-dir', '/root/code-data', '--extensions-dir', '/root/code-ext',
        '--disable-telemetry', '--disable-update-check', '--disable-workspace-trust', '--log', 'debug', '/root/project']
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
                        (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
                        if args.browser:
                            result['browser'] = probe_browser(url, args.output)
                        if args.serve_only:
                            while time.monotonic() - began < args.timeout:
                                if process.poll() is not None:
                                    raise RuntimeError('guest server exited before probe cleanup')
                                time.sleep(1)
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
    if args.browser:
        for guest, filename, expected in [
            ('/root/project/hello.txt', 'saved-file.txt', b'edited through guest code-server'),
            ('/root/project/terminal-result.txt', 'terminal-result.txt', b'TRAMES_TERMINAL_OK'),
        ]:
            target = args.output / filename
            checked = subprocess.run(base + ['--export', guest + '=' + str(target), '--guest', '/bin/true'],
                                     capture_output=True, timeout=120)
            key = 'editing_verified' if filename == 'saved-file.txt' else 'terminal_verified'
            result.setdefault('browser', {})[key] = checked.returncode == 0 and expected in target.read_bytes()
            if checked.returncode:
                result['browser'][key + '_error'] = checked.stderr.decode(errors='replace')
        (args.output / 'browser.json').write_text(json.dumps(result['browser'], indent=2) + '\n')
        (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        print('Guest acceptance:', {key: result['browser'].get(key) for key in ('opened_file', 'editing_verified', 'terminal_verified')}, flush=True)


if __name__ == '__main__':
    main()
