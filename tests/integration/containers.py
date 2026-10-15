#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Selected lifecycle acceptance using original Alpine-signed crun inside the guest.

OCI JSON below is test input to crun, not a Trames OCI launcher. No host runtime,
namespace, cgroup or device is used to implement the container.
"""
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import m6

ROOT = Path(__file__).resolve().parents[2]
RUNNER = os.getenv('TRAMES_RUNNER', str(ROOT/'target/release/trames-run'))
OUT = ROOT/'target/container-lifecycle'
OUT.mkdir(parents=True, exist_ok=True)
config = {
    'ociVersion': '1.0.2', 'root': {'path': 'rootfs', 'readonly': False},
    'process': {'terminal': False, 'user': {'uid': 0, 'gid': 0},
        'args': ['/bin/busybox', 'sh', '-ec',
            'test "$$" = 1; read host < /proc/sys/kernel/hostname; test "$host" = container; '
            'test -r /proc/1/status; echo CONTAINER_PAYLOAD; exit 7'],
        'env': ['PATH=/bin', 'HOME=/root'], 'cwd': '/', 'noNewPrivileges': True,
        'capabilities': {key: ['CAP_CHOWN', 'CAP_SETUID', 'CAP_SETGID']
                         for key in ('bounding', 'effective', 'permitted')}},
    'hostname': 'container',
    'mounts': [{'destination': '/proc', 'type': 'proc', 'source': 'proc',
                'options': ['nosuid', 'noexec', 'nodev']}],
    'linux': {'rootfsPropagation': 'private', 'namespaces': [{'type': key} for key in ('pid', 'mount', 'uts')]},
}
lifecycle = copy.deepcopy(config)
lifecycle['process']['args'] = ['/bin/busybox', 'sh', '-ec',
    'trap "echo TERM_RECEIVED > /terminated; exit 0" TERM; '
    'echo RUNNING > /ready; read line < /tmp/stop']
results = {'scope': 'Original guest crun selected lifecycle, private PID/mount/UTS and proc mount; cgroup manager disabled. Not full OCI conformance.',
           'config': config, 'lifecycle_config': lifecycle, 'checks': []}
logs = []
with tempfile.TemporaryDirectory(prefix='trames-container-lifecycle-') as directory:
    tmp = Path(directory)
    m6.repository(tmp/'repository', ('crun',))
    packages = sorted((tmp/'repository').rglob('*.apk'))
    results['packages_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in packages}
    for name, cfg in (('run', config), ('lifecycle', lifecycle)):
        (tmp/(name+'.json')).write_text(json.dumps(cfg, indent=2)+'\n')
    for cpus in ('1', '4'):
        base = [RUNNER, '--storage', str(tmp/('store-'+cpus)), '--network', 'off', '--cpus', cpus,
                '--max-instructions', '3000000000']
        def run(label, args, expected=0):
            p = subprocess.run([*base, *args], capture_output=True, timeout=120)
            results['checks'].append({'cpus': int(cpus), 'label': label, 'exit_code': p.returncode,
                                     'stdout': p.stdout.decode(), 'stderr': p.stderr.decode()})
            logs.append(f'=== {cpus} CPU {label}; exit={p.returncode} ===\n'+(p.stdout+p.stderr).decode())
            (OUT/'results.json').write_text(json.dumps(results, indent=2)+'\n')
            (OUT/'lifecycle.log').write_text('\n'.join(logs))
            assert p.returncode == expected, (cpus, label, p.returncode, p.stdout, p.stderr)
            return p
        imported = []
        for p in packages:
            imported += ['--file', '/tmp/'+p.name+'='+str(p)]
        p = run('signed install', ['--rootfs', str(m6.CACHE/'alpine-minirootfs-3.22.6-aarch64.tar.gz'),
            *imported, '--guest', '/sbin/apk', '--repositories-file', '/dev/null', '--no-progress',
            'add', *['/tmp/'+p.name for p in packages]])
        assert b'ERROR:' not in p.stdout+p.stderr
        run('version', ['--guest', '/usr/bin/crun', '--version'])
        p = run('payload exit and namespace isolation', [
            '--file', '/run-config.json='+str(tmp/'run.json'),
            '--file', '/lifecycle-config.json='+str(tmp/'lifecycle.json'),
            '--exec-file', '/bundle/rootfs/bin/busybox='+str(ROOT/'target/busybox-build/busybox'),
            '--guest', '/bin/sh', '-ec', '''
mkdir -p /bundle/rootfs/dev /bundle/rootfs/proc /bundle/rootfs/sys /bundle/rootfs/tmp
hostname outside
cp /run-config.json /bundle/config.json
cd /bundle
status=0
crun --root /run/crun --cgroup-manager disabled run payload || status=$?
test "$status" = 7
test "$(hostname)" = outside
test ! -e /bundle/rootfs/proc/1/status
test ! -e /run/crun/payload
echo PAYLOAD_EXIT_AND_NAMESPACE_PASS
'''])
        assert b'CONTAINER_PAYLOAD\n' in p.stdout and b'PAYLOAD_EXIT_AND_NAMESPACE_PASS\n' in p.stdout, p
        p = run('create start state exec TERM stop delete', ['--guest', '/bin/sh', '-ec', '''
hostname outside
cp /lifecycle-config.json /bundle/config.json
cd /bundle
mkfifo rootfs/tmp/stop
crun --root /run/crun --cgroup-manager disabled create lifecycle
state="$(crun --root /run/crun state lifecycle)"
printf '%s\n' "$state"
echo "$state" | grep '"status": "created"'
test ! -e rootfs/ready
crun --root /run/crun start lifecycle
for i in $(seq 1 100); do test -f rootfs/ready && break; sleep .01; done
test "$(cat rootfs/ready)" = RUNNING
state="$(crun --root /run/crun state lifecycle)"
printf '%s\n' "$state"
echo "$state" | grep '"status": "running"'
crun --root /run/crun exec lifecycle /bin/busybox sh -ec 'test "$$" -gt 1; test -r /proc/1/status; read host < /proc/sys/kernel/hostname; test "$host" = container; echo EXEC_PAYLOAD'
crun --root /run/crun kill lifecycle TERM
for i in $(seq 1 100); do test -f rootfs/terminated && break; sleep .01; done
test "$(cat rootfs/terminated)" = TERM_RECEIVED
for i in $(seq 1 100); do state="$(crun --root /run/crun state lifecycle)"; echo "$state" | grep -q '"status": "stopped"' && break; sleep .01; done
printf '%s\n' "$state"
echo "$state" | grep '"status": "stopped"'
crun --root /run/crun delete lifecycle
test ! -e /run/crun/lifecycle
crun --root /run/crun list
test "$(hostname)" = outside
echo CONTAINER_LIFECYCLE_PASS
dmesg
'''])
        assert b'EXEC_PAYLOAD\n' in p.stdout and b'CONTAINER_LIFECYCLE_PASS\n' in p.stdout, p
        print(f'{cpus}-CPU original crun: payload/exit 7, private PID/mount/UTS, proc, create/start/state/exec/TERM/stop/delete passed', flush=True)
results['passed'] = True
(OUT/'results.json').write_text(json.dumps(results, indent=2)+'\n')
print(f'Evidence: {OUT}')
