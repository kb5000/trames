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
    for cpus in ('1', '4'):
        result = subprocess.run([RUNNER, '--storage', str(Path(folder)/('mount-'+cpus)), '--cpus', cpus,
            '--network', 'off', '--max-instructions', '1000000000',
            '--exec-file', f"/mount-namespaces={ROOT/'tests/fixtures/bin/mount-namespaces'}", '--guest', '/mount-namespaces'],
            capture_output=True, timeout=90)
        assert result.returncode == 0 and result.stdout == (
            b'mount: namespace isolation, setns, tmpfs quota, bind identity/readonly/noexec, detached fd: ok\n'
            b'mount: pivot_root, mounted proc, old-root detach, parent root preserved: ok\n'), (cpus, result.returncode, result.stdout, result.stderr[-8000:])
        print(f'{cpus}-CPU guest mount namespace, tmpfs/bind/pivot_root and mount FD lifetime passed', flush=True)
    for cpus in ('1', '4'):
        result = subprocess.run([RUNNER, '--storage', str(Path(folder)/('overlay-'+cpus)), '--cpus', cpus,
            '--network', 'off', '--max-instructions', '1000000000',
            '--exec-file', f"/overlay-lifetime={ROOT/'tests/fixtures/bin/overlay-lifetime'}", '--guest', '/overlay-lifetime'],
            capture_output=True, timeout=90)
        assert result.returncode == 0 and result.stdout == (
            b'overlay lifetime: repeated namespace pivot and workdir reuse: ok\n'), (cpus, result.returncode, result.stdout, result.stderr[-8000:])
        print(f'{cpus}-CPU guest repeated overlay pivot, namespace release and workdir reuse passed', flush=True)
    for cpus in ('1', '4'):
        result = subprocess.run([RUNNER, '--storage', str(Path(folder)/('pid-'+cpus)), '--cpus', cpus,
            '--network', 'off', '--max-instructions', '1000000000',
            '--exec-file', f"/pid-namespaces={ROOT/'tests/fixtures/bin/pid-namespaces'}", '--guest', '/pid-namespaces'],
            capture_output=True, timeout=90)
        assert result.returncode == 0 and result.stdout == (
            b'pid: clone hierarchy, proc mount views, signals/waitid, threads, orphan adoption and init shutdown: ok\n'
            b'pid: unshare/setns child-only semantics, dead namespace rejection and subreaper: ok\n'), (cpus, result.returncode, result.stdout, result.stderr[-8000:])
        print(f'{cpus}-CPU guest PID hierarchy, proc views, signals/waits, threads, init shutdown and subreaper passed', flush=True)
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

    result = subprocess.run([RUNNER, '--storage', str(Path(folder)/'busybox'), '--network', 'off',
        '--max-instructions', '300000000', '--guest', '/bin/sh', '-ec',
        "mkdir -p /m-source /m-target; echo guest > /m-source/data; "
        "unshare -m /bin/sh -ec 'mount -t tmpfs -o size=64k tmpfs /m-target; echo private > /m-target/inside; "
        "df /m-target; umount /m-target; mount -o bind /m-source /m-target; test \"$(cat /m-target/data)\" = guest; "
        "mount -o remount,bind,ro /m-target; umount /m-target'; "
        "test ! -e /m-target/inside; echo BUSYBOX_MOUNTS_PASS"], capture_output=True, timeout=60)
    assert result.returncode == 0 and b'BUSYBOX_MOUNTS_PASS\n' in result.stdout, (result.returncode, result.stdout, result.stderr[-8000:])
    print('Real BusyBox unshare -m, mount tmpfs/bind/remount, df and umount passed', flush=True)

    result = subprocess.run([RUNNER, '--storage', str(Path(folder)/'busybox'), '--network', 'off',
        '--max-instructions', '300000000', '--guest', '/bin/sh', '-ec',
        "unshare -pf --mount-proc /bin/sh -ec 'test \"$$\" = 1; test \"$(readlink /proc/self)\" -gt 1; "
        "sleep 0.01 & wait; ps; test -r /proc/1/status'; echo BUSYBOX_PID_PASS"], capture_output=True, timeout=60)
    assert result.returncode == 0 and b'BUSYBOX_PID_PASS\n' in result.stdout, (result.returncode, result.stdout, result.stderr[-8000:])
    print('Real BusyBox unshare -pf --mount-proc, shell PID 1, background wait and ps passed', flush=True)
