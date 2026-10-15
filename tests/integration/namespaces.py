#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Guest namespace operations and real BusyBox unshare/nsenter/chroot."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
RUNNER = os.environ.get('TRAMES_RUNNER', str(ROOT / 'target/release/trames-run'))
EXPECTED = (b'UTS: fork sharing, clone/unshare isolation, namespace fd/stat/ioctl/setns/lifetime: ok\n'
            b'chroot: independent fs root, absolute symlinks, dotdot, exec, privilege: ok\n')
with tempfile.TemporaryDirectory(prefix='trames-namespaces-') as folder:
    for cpus in ('1', '4'):
        command = [RUNNER, '--storage', str(Path(folder)/cpus), '--cpus', cpus,
                   '--network', 'off', '--max-instructions', '1000000000',
                   "--exec-file", f"/namespaces={ROOT/'tests/fixtures/bin/namespaces'}", "--guest", "/namespaces"]
        result = subprocess.run(command, capture_output=True, timeout=90)
        assert result.returncode == 0 and result.stdout == EXPECTED, (cpus, result.returncode, result.stdout, result.stderr[-8000:])
        print(f'{cpus}-CPU guest UTS namespace, namespace FD lifetime, chroot/exec passed', flush=True)
    command = [RUNNER, '--storage', str(Path(folder)/'busybox'), '--network', 'off',
               '--rootfs', str(ROOT/'target/alpine-download/alpine-minirootfs-3.22.6-aarch64.tar.gz'),
               '--exec-file', f"/jail/bin/sh={ROOT/'target/busybox-build/busybox'}",
               '--max-instructions', '200000000', '--guest', '/bin/sh', '-ec',
               "hostname parent; unshare -u /bin/sh -ec 'hostname isolated; test \"$(hostname)\" = isolated'; "
               "test \"$(hostname)\" = parent; nsenter -u/proc/self/ns/uts /bin/sh -ec 'test \"$(hostname)\" = parent'; "
               "chroot /jail /bin/sh -ec 'test \"$PWD\" = /'; echo BUSYBOX_NAMESPACE_PASS"]
    result = subprocess.run(command, capture_output=True, timeout=60)
    assert result.returncode == 0 and b'BUSYBOX_NAMESPACE_PASS\n' in result.stdout, (result.returncode, result.stdout, result.stderr[-8000:])
    print('Real BusyBox unshare/nsenter/chroot passed', flush=True)
