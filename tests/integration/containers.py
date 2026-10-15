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
config['linux']['cgroupsPath'] = '/test-payload'
lifecycle = copy.deepcopy(config)
lifecycle['linux']['cgroupsPath'] = '/test-lifecycle'
lifecycle['process']['args'] = ['/bin/busybox', 'sh', '-ec',
    'trap "echo TERM_RECEIVED > /terminated; exit 0" TERM; '
    'echo RUNNING > /ready; read line < /tmp/stop']
# Exercise a normal signed Alpine filesystem as well as the small static fixture.
full = copy.deepcopy(lifecycle)
full['linux']['resources'] = {'pids': {'limit': 8}}
full['mounts'] += [
    {'destination':'/dev','type':'tmpfs','source':'tmpfs','options':['nosuid','mode=0755','size=1m']},
    {'destination':'/dev/pts','type':'devpts','source':'devpts','options':['nosuid','noexec','newinstance','mode=0620','ptmxmode=0666','gid=5']},
    {'destination':'/dev/shm','type':'tmpfs','source':'tmpfs','options':['nosuid','nodev','mode=1777','size=1m']},
    {'destination':'/sys','type':'sysfs','source':'sysfs','options':['nosuid','noexec','nodev','ro']},
]
full['process']['args'] = ['/bin/sh','-ec',
    'test "$$" = 1; read host < /proc/sys/kernel/hostname; '
    'echo "$host" > /ready; echo shm > /dev/shm/marker; '
    'trap "echo TERM_RECEIVED > /terminated; exit 0" TERM; read line < /tmp/stop']
full_configs = {}
for name in ('alpha','beta'):
    cfg = copy.deepcopy(full)
    cfg['hostname'] = name
    cfg['linux']['cgroupsPath'] = '/test-'+name
    full_configs[name] = cfg
terminal = copy.deepcopy(full_configs['alpha'])
terminal['hostname'] = 'terminal'
terminal['linux']['cgroupsPath'] = '/test-terminal'
terminal['process']['terminal'] = True
terminal['process']['consoleSize'] = {'height': 31, 'width': 97}
terminal['process']['args'] = ['/bin/sh','-ec',
    'test -t 0; test -t 1; test -t 2; test "$$" = 1; '
    'test "$(/bin/busybox readlink /proc/self/fd/0)" = /dev/pts/0; test "$(/bin/busybox stty size)" = "31 97"; '
    'echo CONTAINER_TERMINAL_PASS; exit 7']
full_configs['terminal'] = terminal
results = {'scope': 'Original guest crun selected lifecycle, private PID/mount/UTS and proc mount; cgroupfs manager with freezer and subtree kill. Not full OCI conformance.',
           'config': config, 'lifecycle_config': lifecycle, 'full_configs': full_configs, 'checks': []}
logs = []
with tempfile.TemporaryDirectory(prefix='trames-container-lifecycle-') as directory:
    tmp = Path(directory)
    m6.repository(tmp/'repository', ('crun',))
    packages = sorted((tmp/'repository').rglob('*.apk'))
    results['packages_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in packages}
    for name, cfg in [('run', config), ('lifecycle', lifecycle), *full_configs.items()]:
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
crun --root /run/crun --cgroup-manager cgroupfs run payload || status=$?
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
crun --root /run/crun --cgroup-manager cgroupfs create lifecycle
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
crun --root /run/crun pause lifecycle
crun --root /run/crun state lifecycle | grep '"status": "paused"'
grep 'frozen 1' /sys/fs/cgroup/test-lifecycle/cgroup.events
crun --root /run/crun resume lifecycle
crun --root /run/crun state lifecycle | grep '"status": "running"'
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
        p = run('concurrent signed Alpine rootfs and freezer', [
            '--file', '/rootfs.tgz='+str(m6.CACHE/'alpine-minirootfs-3.22.6-aarch64.tar.gz'),
            *[arg for name in full_configs for arg in ('--file', '/'+name+'.json='+str(tmp/(name+'.json')))],
            '--guest','/bin/sh','-ec', r'''
for name in alpha beta; do
  mkdir -p /$name/rootfs
  tar -xzf /rootfs.tgz -C /$name/rootfs
  cp /$name.json /$name/config.json
  mkfifo /$name/rootfs/tmp/stop
  crun --root /run/crun --cgroup-manager cgroupfs create --bundle /$name $name
  crun --root /run/crun start $name
done
for name in alpha beta; do
  for i in $(seq 1 100); do test -f /$name/rootfs/ready && break; sleep .01; done
  test "$(cat /$name/rootfs/ready)" = "$name"
  test "$(cat /sys/fs/cgroup/test-$name/pids.max)" = 8
  crun --root /run/crun state $name | grep '"status": "running"'
  crun --root /run/crun exec $name /bin/sh -ec 'test "$$" -gt 1; test -r /proc/1/stat; test "$(cat /dev/shm/marker)" = shm; grep " - devpts " /proc/self/mountinfo; echo EXEC_ALPINE_PASS'
  test ! -e /$name/rootfs/dev/shm/marker
done
crun --root /run/crun pause alpha
crun --root /run/crun state alpha | grep '"status": "paused"'
crun --root /run/crun exec beta /bin/sh -ec 'test "$(hostname)" = beta; echo SIBLING_PROGRESS_PASS'
crun --root /run/crun resume alpha
crun --root /run/crun kill alpha TERM
for i in $(seq 1 100); do test -f /alpha/rootfs/terminated && break; sleep .01; done
test "$(cat /alpha/rootfs/terminated)" = TERM_RECEIVED
crun --root /run/crun delete alpha
crun --root /run/crun pause beta
crun --root /run/crun delete --force beta
test ! -e /run/crun/alpha; test ! -e /run/crun/beta
test ! -e /sys/fs/cgroup/test-alpha; test ! -e /sys/fs/cgroup/test-beta
echo CONCURRENT_ALPINE_PASS
'''])
        assert b'CONCURRENT_ALPINE_PASS\n' in p.stdout, p
        p = run('original crun foreground terminal and exit status', ['--pty','--guest','/bin/sh','-ec', r'''
cp /terminal.json /alpha/config.json
cd /alpha
status=0
crun --root /run/crun --cgroup-manager cgroupfs run terminal || status=$?
test "$status" = 7
test ! -e /run/crun/terminal
echo TERMINAL_EXIT_PASS
'''])
        assert b'CONTAINER_TERMINAL_PASS' in p.stdout and b'TERMINAL_EXIT_PASS' in p.stdout, p
        print(f'{cpus}-CPU original crun: payload/exit 7, private PID/mount/UTS, proc, cgroupfs, pause/resume/kill, two Alpine rootfs containers and foreground PTY passed', flush=True)
results['passed'] = True
(OUT/'results.json').write_text(json.dumps(results, indent=2)+'\n')
print(f'Evidence: {OUT}')
