#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Per-task guest capabilities and unmodified signed Alpine libcap tools."""
import os
from pathlib import Path
import subprocess
import tempfile
import m6

ROOT = Path(__file__).resolve().parents[2]
RUNNER = os.getenv('TRAMES_RUNNER', str(ROOT / 'target/release/trames-run'))
EXPECTED = b'capabilities: ABI, enforcement, UID/exec, bounding/ambient/securebits, NNP, fork/thread/PID namespace: ok\n'
with tempfile.TemporaryDirectory(prefix='trames-capabilities-') as directory:
    tmp = Path(directory)
    native = tmp/'native-caps'
    subprocess.run([os.getenv('CC', 'cc'), '-O2', '-Wall', '-Wextra', '-Werror', '-pthread',
        str(ROOT/'tests/fixtures/capabilities.c'), '-o', str(native)], check=True)
    oracle = subprocess.run([str(native), 'abi-unprivileged'], capture_output=True, check=True).stdout
    for cpus in ('1', '4'):
        fixture = ROOT/'tests/fixtures/bin/capabilities'
        p = subprocess.run([RUNNER, '--storage', str(tmp/cpus), '--cpus', cpus,
            '--network', 'off', '--max-instructions', '500000000',
            '--exec-file', f'/caps={fixture}', '--exec-file', f'/suid={fixture}', '--exec-file', f'/sgid={fixture}',
            '--guest', '/caps'], capture_output=True, timeout=90)
        assert p.returncode == 0 and p.stdout == EXPECTED, (cpus, p.returncode, p.stdout, p.stderr[-8000:])
        p = subprocess.run([RUNNER, '--storage', str(tmp/cpus), '--cpus', cpus,
            '--network', 'off', '--guest', '/caps', 'abi-unprivileged'], capture_output=True, timeout=60)
        assert p.returncode == 0 and p.stdout == oracle, (p.returncode, p.stdout, p.stderr[-4000:])
        print(f'{cpus}-CPU capability ABI, authority enforcement, UID/exec and per-thread inheritance passed', flush=True)
    m6.repository(tmp/'repository', ('libcap-utils',))
    packages = sorted((tmp/'repository').rglob('*.apk'))
    base = [RUNNER, '--storage', str(tmp/'tools'), '--network', 'off', '--cpus', '4', '--max-instructions', '2000000000']
    imported = []
    for package in packages:
        imported += ['--file', '/tmp/' + package.name + '=' + str(package)]
    p = subprocess.run([*base, '--rootfs', str(m6.CACHE/'alpine-minirootfs-3.22.6-aarch64.tar.gz'),
        *imported, '--guest', '/sbin/apk', '--repositories-file', '/dev/null', '--no-progress',
        'add', *['/tmp/'+p.name for p in packages]], capture_output=True, timeout=180)
    assert p.returncode == 0 and b'ERROR:' not in p.stdout+p.stderr, (p.returncode, p.stdout, p.stderr[-2000:])
    script = '''set -eu
capsh --print
capsh --has-p=cap_sys_admin --has-b=cap_net_raw
capsh --drop=cap_net_raw --no-new-privs --print
capsh --keep=1 --uid=1000 --caps=cap_net_raw+ip --addamb=cap_net_raw --shell=/bin/sh -- -c 'grep "^Cap\\|^NoNewPrivs" /proc/self/status'
capsh --drop=cap_sys_admin --shell=/bin/sh -- -c 'if unshare -m /bin/true; then exit 1; fi; echo CAPABILITY_DENIAL_PASS'
capsh --caps= --no-new-privs --shell=/bin/sh -- -c 'if chroot / /bin/true; then exit 1; fi; echo NO_NEW_PRIVS_PASS'
cat /proc/sys/kernel/cap_last_cap
'''
    p = subprocess.run([*base, '--guest', '/bin/sh', '-ec', script], capture_output=True, timeout=90)
    assert p.returncode == 0, (p.returncode, p.stdout, p.stderr[-4000:])
    for expected in (b'CAPABILITY_DENIAL_PASS', b'NO_NEW_PRIVS_PASS', b'CapAmb:\t0000000000002000', b'40\n'):
        assert expected in p.stdout, (expected, p.stdout, p.stderr)
    print(p.stdout.decode(), end='')
    print('Signed original capsh: query, drop, keepcaps/setuid, ambient exec and no_new_privs passed', flush=True)
