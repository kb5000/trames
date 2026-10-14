# SPDX-License-Identifier: GPL-3.0-or-later
"""Owner's Python memfd/pass_fds repro plus libc fexecve and closed-fd errors."""
import errno
import os
import subprocess

with open('/tmp/arm64lab/hello', 'rb') as source:
    elf = source.read()
fd = os.memfd_create('penguin_elf', 0)
try:
    os.write(fd, elf)
    os.lseek(fd, 0, os.SEEK_SET)
    assert os.readlink(f'/proc/self/fd/{fd}') == '/memfd:penguin_elf (deleted)'
    result = subprocess.run([f'/proc/self/fd/{fd}'], pass_fds=(fd,), capture_output=True, text=True, timeout=5)
    assert result.returncode == 0 and result.stdout == 'Hello from ARM64 syscalls\n' and not result.stderr, result
    print('[PASS] Anonymous ELF execution!', flush=True)
    read_end, write_end = os.pipe()
    child = os.fork()
    if child == 0:
        os.close(read_end)
        os.dup2(write_end, 1)
        os.close(write_end)
        os.execve(fd, ['fd-argv0'], os.environ)
        os._exit(99)
    os.close(write_end)
    output = bytearray()
    while True:
        chunk = os.read(read_end, 4096)
        if not chunk:
            break
        output.extend(chunk)
    os.close(read_end)
    _, status = os.waitpid(child, 0)
    assert status == 0 and output == b'Hello from ARM64 syscalls\n'
    print('[PASS] Python os.execve(fd) / libc fexecve!', flush=True)
finally:
    os.close(fd)
try:
    subprocess.run([f'/proc/self/fd/{fd}'], capture_output=True, timeout=5)
except FileNotFoundError as error:
    assert error.errno == errno.ENOENT
else:
    raise AssertionError('Closed descriptor must not retain a proc fd link')
print('[PASS] Closed fd returns ENOENT!', flush=True)
