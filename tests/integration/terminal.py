#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare interactive outcomes with QEMU ash on a real host PTY."""
import errno, os, pty, select, shlex, shutil, signal, time, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]

class Session:
    def __init__(self, cmd, env):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.execve(cmd[0], cmd, env)
        self.log = b''
    def send(self, data):
        os.write(self.fd, data)
    def until(self, needle, timeout=10):
        data = b''; end = time.monotonic() + timeout
        while time.monotonic() < end:
            if select.select([self.fd], [], [], .05)[0]:
                try:
                    chunk = os.read(self.fd, 65536)
                except OSError as e:
                    if e.errno == errno.EIO:
                        break
                    raise
                if not chunk:
                    break
                data += chunk; self.log += chunk
                if needle in data:
                    return data
        raise AssertionError(f'PTY timed out waiting for {needle!r}; tail {self.log[-1500:]!r}')
    def close(self):
        try:
            os.kill(self.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        os.waitpid(self.pid, 0); os.close(self.fd)

def exercise(cmd, env):
    s = Session(cmd, env); facts = []
    try:
        s.until(b'$ '); facts.append('prompt')
        s.send(b'echo EDIX\x7fT\n'); out = s.until(b'$ ')
        assert b'EDIT\r\n' in out, (out, 'erase'); facts.append('editing')
        s.send(b'echo PIPE | cat\n'); out = s.until(b'$ ')
        assert b'PIPE\r\n' in out, (out, 'pipeline'); facts.append('pipeline')
        s.send(b'sleep 20\n'); time.sleep(.2); s.send(b'\x03'); s.until(b'$ ')
        s.send(b'echo RC:$?\n'); out = s.until(b'$ ')
        assert b'RC:130\r\n' in out, (out, 'interrupt'); facts.append('Ctrl-C 130')
        s.send(b'sleep 20\n'); time.sleep(.2); s.send(b'\x1a'); out = s.until(b'$ ')
        assert b'Stopped' in out, (out, 'stop'); facts.append('Ctrl-Z stopped')
        s.send(b'jobs\n'); out = s.until(b'$ ')
        assert b'Stopped' in out, (out, 'jobs'); facts.append('jobs stopped')
        s.send(b'bg\n'); out = s.until(b'$ ')
        assert b'Done' not in out and b'No current job' not in out, (out, 'bg')
        facts.append('bg resumed')
        s.send(b'fg\n'); time.sleep(.2); s.send(b'\x03'); s.until(b'$ ')
        s.send(b'echo FG:$?\n'); out = s.until(b'$ ')
        assert b'FG:130\r\n' in out, (out, 'fg'); facts.append('fg interrupt 130')
        s.send(b'\x04'); end = time.monotonic() + 5
        while time.monotonic() < end:
            pid, status = os.waitpid(s.pid, os.WNOHANG)
            if pid:
                assert os.waitstatus_to_exitcode(status) == 0, status
                s.pid = 0; facts.append('EOF exit 0'); break
            time.sleep(.02)
        else:
            raise AssertionError('PTY shell did not exit on Ctrl-D')
    finally:
        if s.pid:
            s.close()
        else:
            os.close(s.fd)
    return facts

def main():
    runner = str(ROOT / 'target/debug/trames-run')
    busybox = str(ROOT / 'target/busybox-build/busybox')
    qemu = shutil.which(os.getenv('QEMU_AARCH64', 'qemu-aarch64'))
    if not qemu:
        raise RuntimeError('QEMU unavailable')
    with tempfile.TemporaryDirectory(prefix='trames-pty-') as folder:
        bin_ = Path(folder) / 'bin'; bin_.mkdir()
        for name in ['sleep', 'cat']:
            p = bin_ / name
            p.write_text('#!/bin/sh\nexec ' + shlex.join([qemu, '-cpu', 'cortex-a53', busybox, name]) + ' "$@"\n')
            p.chmod(0o755)
        actual = exercise([runner, '--install-busybox', '--pty', busybox, 'ash', '-i'],
                          {'PATH': '/usr/bin:/bin', 'LANG': 'C', 'TERM': 'xterm'})
        reference = exercise([qemu, '-cpu', 'cortex-a53', busybox, 'ash', '-i'],
                             {'PATH': str(bin_), 'LANG': 'C', 'TERM': 'xterm'})
        if actual != reference:
            raise AssertionError((actual, reference))
    print(f'PTY integration passed: {len(actual)} interactive outcomes matched QEMU ash')

if __name__ == '__main__':
    main()
