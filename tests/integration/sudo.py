#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unmodified signed sudo: default use_pty, credentials, passwords and relay."""
import importlib.util
import os
from pathlib import Path
import selectors
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
RUNNER = Path(os.getenv('TRAMES_RUNNER', ROOT / 'target/release/trames-run'))
spec = importlib.util.spec_from_file_location('m6_repository', ROOT / 'tests/integration/m6.py')
m6 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m6)


class Session:
    def __init__(self, command):
        # Keep the embedding master open; communicate() closing stdin would
        # correctly make sudo observe a disconnected outer terminal.
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.output = bytearray()
        self.errors = bytearray()
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ, self.output)
        self.selector.register(self.process.stderr, selectors.EVENT_READ, self.errors)

    def pump(self, timeout=.1):
        for key, _ in self.selector.select(timeout):
            chunk = os.read(key.fd, 65536)
            if chunk:
                key.data.extend(chunk)
            else:
                self.selector.unregister(key.fileobj)

    def until(self, needle, timeout=45):
        deadline = time.monotonic() + timeout
        while needle not in self.output:
            self.pump()
            assert self.process.poll() is None or self.selector.get_map(), (self.output, self.errors[-2000:])
            assert time.monotonic() < deadline, (needle, self.output[-2000:], self.errors[-2000:])

    def send(self, data):
        self.process.stdin.write(data)
        self.process.stdin.flush()

    def finish(self, expected=0, timeout=45):
        deadline = time.monotonic() + timeout
        while self.process.poll() is None or self.selector.get_map():
            self.pump()
            assert time.monotonic() < deadline, (self.output[-2000:], self.errors[-2000:])
        assert self.process.returncode == expected, (self.process.returncode, self.output[-2000:], self.errors[-2000:])
        return bytes(self.output), bytes(self.errors)

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait()
        self.selector.close()
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            stream.close()


def main():
    with tempfile.TemporaryDirectory(prefix='trames-sudo-') as directory:
        tmp = Path(directory)
        m6.repository(tmp / 'repository', ('sudo',))
        packages = sorted((tmp / 'repository').rglob('*.apk'))
        base = [str(RUNNER), '--storage', str(tmp / 'store'), '--cpus',
            os.getenv('TRAMES_TEST_CPUS', '4'), '--max-instructions', '10000000000']
        if os.getenv('TRAMES_TEST_MUSL'):
            base += ['--musl-libc', os.environ['TRAMES_TEST_MUSL'], '--accelerator', 'pure']
        imported = []
        for package in packages:
            imported += ['--file', '/tmp/' + package.name + '=' + str(package)]
        install = subprocess.run([*base, '--rootfs', str(m6.CACHE / 'alpine-minirootfs-3.22.6-aarch64.tar.gz'),
            *imported, '--guest', '/sbin/apk', '--repositories-file', '/dev/null', '--no-progress',
            'add', *['/tmp/' + p.name for p in packages]], capture_output=True, timeout=300)
        assert install.returncode == 0 and b'ERROR:' not in install.stdout + install.stderr, install
        print('Signed sudo install: passed', flush=True)
        setup = '''set -eu
adduser -D sudoer; adduser -D authuser; adduser -D denied
printf 'authuser:fixture-password\n' | chpasswd
printf '\nsudoer ALL=(ALL:ALL) NOPASSWD: ALL\nauthuser ALL=(root) ALL\n' >> /etc/sudoers
/usr/sbin/visudo -c
printf protected >/root/sudo-private; chmod 600 /root/sudo-private
'''
        p = subprocess.run([*base, '--guest', '/bin/sh', '-c', setup], capture_output=True, timeout=60)
        assert p.returncode == 0, (p.stdout, p.stderr)

        def check(arguments, *, expected=0, needle=None):
            session = Session([*base, '--pty', '--guest', *arguments])
            try:
                out, err = session.finish(expected)
                if needle is not None:
                    assert needle in out, (arguments, out, err)
                assert b'Not supported' not in out + err, (out, err)
                return out
            finally:
                session.close()

        check(['/usr/bin/sudo', '-n', '/usr/bin/id'], needle=b'uid=0(root)')
        check(['/usr/bin/sudo', '-n', '-u', 'sudoer', '/usr/bin/id'], needle=b'uid=1000(sudoer)')
        check(['/bin/su', '-', 'sudoer', '-c', 'sudo -n cat /root/sudo-private'], needle=b'protected')
        check(['/usr/bin/sudo', '-n', '/usr/bin/sudo', '-n', '/usr/bin/id'], needle=b'uid=0(root)')
        print('Root/run-as/user escalation and nested PTYs: passed', flush=True)
        check(['/bin/su', '-', 'denied', '-c', 'sudo -n sh -c "echo WRONG >/root/forbidden"'], expected=1)
        check(['/bin/sh', '-c', 'test ! -e /root/forbidden; su - denied -c "test ! -r /root/sudo-private"'])
        print('Denied user cannot execute or read root-only files: passed', flush=True)
        check(['/bin/su', '-', 'authuser', '-c', 'sudo -n true'], expected=1, needle=b'password')
        session = Session([*base, '--pty', '--guest', '/bin/su', '-', 'authuser', '-c', 'sudo -k id'])
        try:
            session.until(b'password for authuser:')
            session.send(b'fixture-password\n')
            out, err = session.finish()
            assert b'uid=0(root)' in out and b'fixture-password' not in out, (out, err)
        finally:
            session.close()
        print('Real passwd authentication and no password echo: passed', flush=True)
        session = Session([*base, '--pty', '--guest', '/usr/bin/sudo', '-n', '/bin/sh', '-c',
            'printf READY; read value; printf "RELAY:%s\\n" "$value"; exit 7'])
        try:
            session.until(b'READY')
            session.send(b'guest-input\n')
            out, err = session.finish(expected=7)
            assert b'RELAY:guest-input' in out, (out, err)
        finally:
            session.close()
        print('Interactive input/output and command exit status: passed', flush=True)
        session = Session([*base, '--pty', '--guest', '/usr/bin/sudo', '-n', '/bin/sh', '-c',
            'printf INTERRUPT-READY; exec sleep 30'])
        try:
            session.until(b'INTERRUPT-READY')
            session.send(b'\x03')
            session.finish(expected=130)
        finally:
            session.close()
        print('Ctrl-C reaches the foreground command: passed', flush=True)
        # Reopen the same store without a PTY: normal redirected sudo remains usable.
        p = subprocess.run([*base, '--guest', '/usr/bin/sudo', '-n', '/usr/bin/id'], capture_output=True, timeout=60)
        assert p.returncode == 0 and b'uid=0(root)' in p.stdout, (p.stdout, p.stderr)
        print('Non-PTY invocation and persistent reopen: passed', flush=True)


if __name__ == '__main__':
    main()
